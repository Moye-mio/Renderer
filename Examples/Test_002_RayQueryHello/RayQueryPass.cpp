// ============================================================================
// 0xx_RayQueryHello - RayQueryPass.cpp
// 仅依赖 TitusRHI 后端无关抽象。VK 后端 CreateShaderImpl 会嗅探 magic word，
// 对 GLSL 文本自动走 glslang 在线编译（SPIR-V 1.5，支持 GL_EXT_ray_query），
// 因此本示例直接提供 .glsl 源码、无需预编译 .spv。
// ============================================================================
#include "RayQueryPass.h"

#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

RayQueryPass::RayQueryPass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void RayQueryPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    // ----------------------------------------------------------------------
    // 0) 能力探测：不支持光追时优雅降级（需求 13.3）
    // ----------------------------------------------------------------------
    const GCaps& caps = device.GetCaps();
    mRayTracingSupported = caps.supportsRayTracing && caps.supportsRayQuery;
    if (!mRayTracingSupported)
    {
        LOG_STREAM_WARN("RayQueryPass")
            << "Current backend/device does not support Ray Query (supportsRayTracing="
            << caps.supportsRayTracing << ", supportsRayQuery=" << caps.supportsRayQuery
            << "). Example will only clear the screen. Run on a GPU + Vulkan backend with KHR ray tracing support.";
        return;
    }

    mWidth  = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowWidth());
    mHeight = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowHeight());
    if (mWidth  == 0) mWidth  = 1280;
    if (mHeight == 0) mHeight = 720;

    // ----------------------------------------------------------------------
    // 1) 三角形顶点缓冲（作为 BLAS 几何输入）
    //    usage 必须含 ShaderDeviceAddress + AccelerationStructureBuildInput，
    //    以便后端取顶点缓冲设备地址并作为 AS 构建只读输入。
    // ----------------------------------------------------------------------
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
        bd.debugName = "RayQueryPass.TriangleVB";
        mVertexBuffer = device.CreateBuffer(bd);
    }

    // ----------------------------------------------------------------------
    // 2) 构建 BLAS（单三角形，无索引）
    // ----------------------------------------------------------------------
    {
        AccelerationStructureDesc blasDesc{};
        blasDesc.type = AccelerationStructureType::BottomLevel;
        blasDesc.buildFlags = ASBuildFlags::PreferFastTrace;

        BLASGeometryDesc geo{};
        geo.vertexBuffer = mVertexBuffer;
        geo.vertexFormat = Format::R32G32B32_SFLOAT;
        geo.vertexStride = sizeof(float) * 3;
        geo.vertexCount  = 3;
        geo.opaque = true;
        blasDesc.geometries.push_back(geo);
        blasDesc.debugName = "RayQueryPass.BLAS";

        mBLAS = device.CreateAccelerationStructure(blasDesc);
    }

    // ----------------------------------------------------------------------
    // 3) 构建 TLAS（单 instance 引用 BLAS，单位变换）
    // ----------------------------------------------------------------------
    if (mBLAS.IsValid())
    {
        AccelerationStructureDesc tlasDesc{};
        tlasDesc.type = AccelerationStructureType::TopLevel;
        tlasDesc.buildFlags = ASBuildFlags::PreferFastTrace;

        TLASInstanceDesc inst{};
        inst.blas = mBLAS;              // transform 默认单位阵
        inst.mask = 0xFF;
        tlasDesc.instances.push_back(inst);
        tlasDesc.debugName = "RayQueryPass.TLAS";

        mTLAS = device.CreateAccelerationStructure(tlasDesc);
    }

    if (!mBLAS.IsValid() || !mTLAS.IsValid())
    {
        LOG_STREAM_ERROR("RayQueryPass") << "Acceleration structure build failed, example falls back to clear-only.";
        mRayTracingSupported = false;
        return;
    }

    // ----------------------------------------------------------------------
    // 4) storage image（compute 写入 + 后续采样显示）
    // ----------------------------------------------------------------------
    {
        TextureDesc td{};
        td.type   = TextureType::Tex2D;
        td.format = Format::R8G8B8A8_UNORM;
        td.width  = mWidth;
        td.height = mHeight;
        td.usage  = TextureUsage::Storage | TextureUsage::Sampled;
        td.debugName = "RayQueryPass.StorageImage";
        mStorageImage = device.CreateTexture(td);
    }
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "RayQueryPass.Sampler";
        mSampler = device.CreateSampler(sd);
    }

    // ----------------------------------------------------------------------
    // 5) compute 管线（rayQuery）
    // ----------------------------------------------------------------------
    const std::string shaderDir = std::string(SOLUTION_DIR) + "Examples/Test_002_RayQueryHello/Shader/";
    {
        std::vector<uint8_t> csBytes;
        if (TitusAsset::ReadAllBytes(shaderDir + "raytrace.comp.glsl", csBytes))
        {
            ShaderDesc cs{};
            cs.stage = ShaderStage::Compute;
            cs.code  = csBytes.data();
            cs.bytes = csBytes.size();
            cs.debugName = "RayQueryPass.CS";
            mComputeShader = device.CreateShader(cs);
        }

        if (mComputeShader.IsValid())
        {
            ComputePipelineDesc cpd{};
            cpd.computeShader = mComputeShader;
            cpd.debugName = "RayQueryPass.ComputePipeline";

            ResourceBinding img{};
            img.name = "u_Output";
            img.set = 0; img.binding = 0;
            img.type = ResourceBindingType::StorageTexture;
            img.stages = ShaderStage::Compute;
            cpd.resourceBindings.push_back(img);

            ResourceBinding tlas{};
            tlas.name = "u_TLAS";
            tlas.set = 0; tlas.binding = 1;
            tlas.type = ResourceBindingType::AccelerationStructure;
            tlas.stages = ShaderStage::Compute;
            cpd.resourceBindings.push_back(tlas);

            mComputePipeline = device.CreatePipeline(cpd);
        }
    }

    // ----------------------------------------------------------------------
    // 6) 显示管线（全屏三角形采样 storage image）
    // ----------------------------------------------------------------------
    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (TitusAsset::ReadAllBytes(shaderDir + "blit.vert.glsl", vsBytes) &&
            TitusAsset::ReadAllBytes(shaderDir + "blit.frag.glsl", fsBytes))
        {
            ShaderDesc vs{};
            vs.stage = ShaderStage::Vertex;
            vs.code  = vsBytes.data();
            vs.bytes = vsBytes.size();
            vs.debugName = "RayQueryPass.BlitVS";
            mBlitVS = device.CreateShader(vs);

            ShaderDesc fs{};
            fs.stage = ShaderStage::Fragment;
            fs.code  = fsBytes.data();
            fs.bytes = fsBytes.size();
            fs.debugName = "RayQueryPass.BlitFS";
            mBlitFS = device.CreateShader(fs);
        }

        if (mBlitVS.IsValid() && mBlitFS.IsValid())
        {
            GraphicsPipelineDesc gp{};
            gp.vertexShader   = mBlitVS;
            gp.fragmentShader = mBlitFS;
            gp.topology       = PrimitiveTopology::TriangleList;
            gp.rasterizer.cullMode = CullMode::None;
            gp.depthStencil.depthTestEnable  = false;
            gp.depthStencil.depthWriteEnable = false;
            gp.rtLayout.colorFormats = { Format::B8G8R8A8_UNORM };

            ResourceBinding tex{};
            tex.name = "u_Tex";
            tex.set = 0; tex.binding = 0;
            tex.type = ResourceBindingType::CombinedImageSampler;
            tex.stages = ShaderStage::Fragment;
            gp.resourceBindings.push_back(tex);
            gp.debugName = "RayQueryPass.BlitPipeline";

            mBlitPipeline = device.CreatePipeline(gp);
        }
    }

    constexpr uint32_t LOCAL = 16;
    mGroupCountX = (mWidth  + LOCAL - 1) / LOCAL;
    mGroupCountY = (mHeight + LOCAL - 1) / LOCAL;

    LOG_STREAM_INFO("RayQueryPass") << "Ray Query example initialized, "
        << mWidth << "x" << mHeight;
}

void RayQueryPass::Destroy(TitusRHI::IGDevice& device)
{
    if (mBlitPipeline.IsValid())    device.Destroy(mBlitPipeline);
    if (mBlitFS.IsValid())          device.Destroy(mBlitFS);
    if (mBlitVS.IsValid())          device.Destroy(mBlitVS);
    if (mComputePipeline.IsValid()) device.Destroy(mComputePipeline);
    if (mComputeShader.IsValid())   device.Destroy(mComputeShader);
    if (mSampler.IsValid())         device.Destroy(mSampler);
    if (mStorageImage.IsValid())    device.Destroy(mStorageImage);
    if (mTLAS.IsValid())            device.Destroy(mTLAS);
    if (mBLAS.IsValid())            device.Destroy(mBLAS);
    if (mVertexBuffer.IsValid())    device.Destroy(mVertexBuffer);

    mBlitPipeline = {}; mBlitFS = {}; mBlitVS = {};
    mComputePipeline = {}; mComputeShader = {};
    mSampler = {}; mStorageImage = {};
    mTLAS = {}; mBLAS = {}; mVertexBuffer = {};
}

void RayQueryPass::Record(TitusRHI::IGDevice&        /*device*/,
                          TitusRHI::RenderCommandList& cmd,
                          uint32_t                       /*frameIndex*/,
                          uint32_t                       /*imageIndex*/)
{
    using namespace TitusRHI;

    // 不支持光追或初始化失败：仅清屏（需求 13.3）
    if (!mRayTracingSupported || !mComputePipeline.IsValid() || !mBlitPipeline.IsValid())
    {
        RenderPassBeginInfo rp{};
        RenderPassAttachmentOp op{};
        op.loadOp  = LoadOp::Clear;
        op.storeOp = StoreOp::Store;
        op.clearValue.color[0] = 0.20f;
        op.clearValue.color[1] = 0.02f;
        op.clearValue.color[2] = 0.02f;
        op.clearValue.color[3] = 1.0f;
        rp.colorOps.push_back(op);
        cmd.BeginRenderPass(rp);
        cmd.EndRenderPass();
        return;
    }

    // ---- 1) compute：rayQuery 求交写 storage image ----
    cmd.BindPipeline(mComputePipeline);
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
    cmd.Dispatch(mGroupCountX, mGroupCountY, 1);

    // ---- 2) 屏障：compute 写 → fragment 采样读 ----
    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ComputeShader;
        bar.dstStage = PipelineStage::FragmentShader;
        bar.srcGlobalAccess = AccessFlags::ShaderWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    }

    // ---- 3) 显示：全屏三角形采样 storage image ----
    {
        RenderPassBeginInfo rp{};
        RenderPassAttachmentOp op{};
        op.loadOp  = LoadOp::Clear;
        op.storeOp = StoreOp::Store;
        op.clearValue.color[0] = 0.0f;
        op.clearValue.color[1] = 0.0f;
        op.clearValue.color[2] = 0.0f;
        op.clearValue.color[3] = 1.0f;
        rp.colorOps.push_back(op);
        cmd.BeginRenderPass(rp);

        Viewport vp{};
        vp.width  = static_cast<float>(mWidth);
        vp.height = static_cast<float>(mHeight);
        cmd.SetViewport(vp);
        Rect2D sc{};
        sc.width  = mWidth;
        sc.height = mHeight;
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
