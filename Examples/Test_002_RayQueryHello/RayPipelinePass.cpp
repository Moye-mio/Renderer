// ============================================================================
// 0xx_RayQueryHello - RayPipelinePass.cpp（RT Pipeline / 路线 B）
// 仅依赖 TitusRHI 抽象。RT 着色器（.rgen/.rmiss/.rchit）由 VK 后端运行期
// 以 SPIR-V 1.5 + GL_EXT_ray_tracing 在线编译。
// ============================================================================
#include "RayPipelinePass.h"

#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

RayPipelinePass::RayPipelinePass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void RayPipelinePass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    const GCaps& caps = device.GetCaps();
    if (!caps.supportsRayTracing || !caps.supportsRayTracingPipeline)
    {
        LOG_STREAM_WARN("RayPipelinePass")
            << "Current backend/device does not support Ray Tracing Pipeline (supportsRayTracingPipeline="
            << caps.supportsRayTracingPipeline << "), example will only clear the screen.";
        return;
    }

    mWidth  = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowWidth());
    mHeight = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowHeight());
    if (mWidth  == 0) mWidth  = 1280;
    if (mHeight == 0) mHeight = 720;

    // 1) 三角形顶点缓冲
    const float verts[9] = {
         0.0f,  0.8f, 0.0f,
        -0.8f, -0.6f, 0.0f,
         0.8f, -0.6f, 0.0f,
    };
    {
        BufferDesc bd{};
        bd.size = sizeof(verts);
        bd.usage = BufferUsage::VertexBuffer
                 | BufferUsage::ShaderDeviceAddress
                 | BufferUsage::AccelerationStructureBuildInput;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.initialData = verts;
        bd.debugName = "RayPipelinePass.TriangleVB";
        mVertexBuffer = device.CreateBuffer(bd);
    }

    // 2) BLAS + 3) TLAS
    {
        AccelerationStructureDesc blasDesc{};
        blasDesc.type = AccelerationStructureType::BottomLevel;
        BLASGeometryDesc geo{};
        geo.vertexBuffer = mVertexBuffer;
        geo.vertexFormat = Format::R32G32B32_SFLOAT;
        geo.vertexStride = sizeof(float) * 3;
        geo.vertexCount  = 3;
        blasDesc.geometries.push_back(geo);
        blasDesc.debugName = "RayPipelinePass.BLAS";
        mBLAS = device.CreateAccelerationStructure(blasDesc);
    }
    if (mBLAS.IsValid())
    {
        AccelerationStructureDesc tlasDesc{};
        tlasDesc.type = AccelerationStructureType::TopLevel;
        TLASInstanceDesc inst{};
        inst.blas = mBLAS;
        tlasDesc.instances.push_back(inst);
        tlasDesc.debugName = "RayPipelinePass.TLAS";
        mTLAS = device.CreateAccelerationStructure(tlasDesc);
    }
    if (!mBLAS.IsValid() || !mTLAS.IsValid())
    {
        LOG_STREAM_ERROR("RayPipelinePass") << "Acceleration structure build failed, falling back to clear-only.";
        return;
    }

    // 4) storage image + sampler
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R8G8B8A8_UNORM;
        td.width = mWidth; td.height = mHeight;
        td.usage = TextureUsage::Storage | TextureUsage::Sampled;
        td.debugName = "RayPipelinePass.StorageImage";
        mStorageImage = device.CreateTexture(td);
    }
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "RayPipelinePass.Sampler";
        mSampler = device.CreateSampler(sd);
    }

    const std::string shaderDir = std::string(SOLUTION_DIR) + "Examples/Test_002_RayQueryHello/Shader/";

    // 5) RT 管线：raygen + miss + closesthit（3 个 group）
    {
        std::vector<uint8_t> rgen, rmiss, rchit;
        TitusAsset::ReadAllBytes(shaderDir + "raygen.rgen.glsl", rgen);
        TitusAsset::ReadAllBytes(shaderDir + "miss.rmiss.glsl", rmiss);
        TitusAsset::ReadAllBytes(shaderDir + "closesthit.rchit.glsl", rchit);

        auto mkShader = [&](std::vector<uint8_t>& bytes, ShaderStage stage, const char* name)
        {
            if (bytes.empty()) return ShaderHandle{};
            ShaderDesc sd{};
            sd.stage = stage;
            sd.code = bytes.data();
            sd.bytes = bytes.size();
            sd.debugName = name;
            return device.CreateShader(sd);
        };
        mRayGen     = mkShader(rgen,  ShaderStage::RayGen,     "RayPipelinePass.RGen");
        mMiss       = mkShader(rmiss, ShaderStage::Miss,       "RayPipelinePass.Miss");
        mClosestHit = mkShader(rchit, ShaderStage::ClosestHit, "RayPipelinePass.CHit");

        if (mRayGen.IsValid() && mMiss.IsValid() && mClosestHit.IsValid())
        {
            RayTracingPipelineDesc rtd{};
            rtd.maxRayRecursionDepth = 1;
            rtd.debugName = "RayPipelinePass.RTPipeline";

            // stages：0=raygen, 1=miss, 2=closesthit
            rtd.stages.push_back({ mRayGen,     ShaderStage::RayGen });
            rtd.stages.push_back({ mMiss,       ShaderStage::Miss });
            rtd.stages.push_back({ mClosestHit, ShaderStage::ClosestHit });

            // groups：general(raygen) / general(miss) / triangles-hit(closesthit)
            RayTracingShaderGroupDesc gRaygen{};
            gRaygen.type = RayTracingShaderGroupType::General;
            gRaygen.generalShader = 0;
            rtd.groups.push_back(gRaygen);

            RayTracingShaderGroupDesc gMiss{};
            gMiss.type = RayTracingShaderGroupType::General;
            gMiss.generalShader = 1;
            rtd.groups.push_back(gMiss);

            RayTracingShaderGroupDesc gHit{};
            gHit.type = RayTracingShaderGroupType::TrianglesHit;
            gHit.closestHitShader = 2;
            rtd.groups.push_back(gHit);

            // 资源绑定：binding0=StorageImage（raygen），binding1=TLAS（raygen）
            ResourceBinding img{};
            img.name = "u_Output"; img.set = 0; img.binding = 0;
            img.type = ResourceBindingType::StorageTexture;
            img.stages = ShaderStage::RayGen;
            rtd.resourceBindings.push_back(img);

            ResourceBinding tlas{};
            tlas.name = "u_TLAS"; tlas.set = 0; tlas.binding = 1;
            tlas.type = ResourceBindingType::AccelerationStructure;
            tlas.stages = ShaderStage::RayGen;
            rtd.resourceBindings.push_back(tlas);

            mRTPipeline = device.CreatePipeline(rtd);
        }
    }

    if (!mRTPipeline.IsValid())
    {
        LOG_STREAM_ERROR("RayPipelinePass") << "RT pipeline creation failed, falling back to clear-only.";
        return;
    }

    // 6) 显示管线
    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (TitusAsset::ReadAllBytes(shaderDir + "blit.vert.glsl", vsBytes) &&
            TitusAsset::ReadAllBytes(shaderDir + "blit.frag.glsl", fsBytes))
        {
            ShaderDesc vs{};
            vs.stage = ShaderStage::Vertex; vs.code = vsBytes.data(); vs.bytes = vsBytes.size();
            vs.debugName = "RayPipelinePass.BlitVS";
            mBlitVS = device.CreateShader(vs);
            ShaderDesc fs{};
            fs.stage = ShaderStage::Fragment; fs.code = fsBytes.data(); fs.bytes = fsBytes.size();
            fs.debugName = "RayPipelinePass.BlitFS";
            mBlitFS = device.CreateShader(fs);
        }
        if (mBlitVS.IsValid() && mBlitFS.IsValid())
        {
            GraphicsPipelineDesc gp{};
            gp.vertexShader = mBlitVS;
            gp.fragmentShader = mBlitFS;
            gp.topology = PrimitiveTopology::TriangleList;
            gp.rasterizer.cullMode = CullMode::None;
            gp.depthStencil.depthTestEnable = false;
            gp.depthStencil.depthWriteEnable = false;
            gp.rtLayout.colorFormats = { Format::B8G8R8A8_UNORM };
            ResourceBinding tex{};
            tex.name = "u_Tex"; tex.set = 0; tex.binding = 0;
            tex.type = ResourceBindingType::CombinedImageSampler;
            tex.stages = ShaderStage::Fragment;
            gp.resourceBindings.push_back(tex);
            gp.debugName = "RayPipelinePass.BlitPipeline";
            mBlitPipeline = device.CreatePipeline(gp);
        }
    }

    mReady = mRTPipeline.IsValid() && mBlitPipeline.IsValid();
    if (mReady)
        LOG_STREAM_INFO("RayPipelinePass") << "RT Pipeline example initialized, "
            << mWidth << "x" << mHeight;
}

void RayPipelinePass::Destroy(TitusRHI::IGDevice& device)
{
    if (mBlitPipeline.IsValid()) device.Destroy(mBlitPipeline);
    if (mBlitFS.IsValid())       device.Destroy(mBlitFS);
    if (mBlitVS.IsValid())       device.Destroy(mBlitVS);
    if (mRTPipeline.IsValid())   device.Destroy(mRTPipeline);
    if (mClosestHit.IsValid())   device.Destroy(mClosestHit);
    if (mMiss.IsValid())         device.Destroy(mMiss);
    if (mRayGen.IsValid())       device.Destroy(mRayGen);
    if (mSampler.IsValid())      device.Destroy(mSampler);
    if (mStorageImage.IsValid()) device.Destroy(mStorageImage);
    if (mTLAS.IsValid())         device.Destroy(mTLAS);
    if (mBLAS.IsValid())         device.Destroy(mBLAS);
    if (mVertexBuffer.IsValid()) device.Destroy(mVertexBuffer);

    mBlitPipeline = {}; mBlitFS = {}; mBlitVS = {};
    mRTPipeline = {}; mClosestHit = {}; mMiss = {}; mRayGen = {};
    mSampler = {}; mStorageImage = {}; mTLAS = {}; mBLAS = {}; mVertexBuffer = {};
}

void RayPipelinePass::Record(TitusRHI::IGDevice&        /*device*/,
                             TitusRHI::RenderCommandList& cmd,
                             uint32_t                       /*frameIndex*/,
                             uint32_t                       /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!mReady)
    {
        RenderPassBeginInfo rp{};
        RenderPassAttachmentOp op{};
        op.loadOp = LoadOp::Clear; op.storeOp = StoreOp::Store;
        op.clearValue.color[0] = 0.20f; op.clearValue.color[3] = 1.0f;
        rp.colorOps.push_back(op);
        cmd.BeginRenderPass(rp);
        cmd.EndRenderPass();
        return;
    }

    // 1) TraceRays：raygen → miss/closesthit，写 storage image
    cmd.BindPipeline(mRTPipeline);
    {
        ResourceSetDesc rs{};
        ResourceBindingValue img{};
        img.binding = 0;
        img.type = ResourceBindingType::StorageTexture;
        img.texture = mStorageImage;
        rs.bindings.push_back(img);

        ResourceBindingValue tlas{};
        tlas.binding = 1;
        tlas.type = ResourceBindingType::AccelerationStructure;
        tlas.accelStruct = mTLAS;
        rs.bindings.push_back(tlas);

        cmd.BindResourceSet(0, rs);
    }
    cmd.TraceRays(mWidth, mHeight, 1);

    // 2) 屏障：raygen 写 → fragment 采样读
    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ComputeShader; // RT 写入近似按 compute stage 处理
        bar.dstStage = PipelineStage::FragmentShader;
        bar.srcGlobalAccess = AccessFlags::ShaderWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    }

    // 3) 显示
    {
        RenderPassBeginInfo rp{};
        RenderPassAttachmentOp op{};
        op.loadOp = LoadOp::Clear; op.storeOp = StoreOp::Store;
        op.clearValue.color[3] = 1.0f;
        rp.colorOps.push_back(op);
        cmd.BeginRenderPass(rp);

        Viewport vp{}; vp.width = static_cast<float>(mWidth); vp.height = static_cast<float>(mHeight);
        cmd.SetViewport(vp);
        Rect2D sc{}; sc.width = mWidth; sc.height = mHeight;
        cmd.SetScissor(sc);

        cmd.BindPipeline(mBlitPipeline);
        {
            ResourceSetDesc rs{};
            ResourceBindingValue tex{};
            tex.binding = 0;
            tex.type = ResourceBindingType::CombinedImageSampler;
            tex.texture = mStorageImage;
            tex.sampler = mSampler;
            rs.bindings.push_back(tex);
            cmd.BindResourceSet(0, rs);
        }
        cmd.Draw(3);
        cmd.EndRenderPass();
    }
}
