// ============================================================================
// 0xx_RayQueryHello - DynamicScenePass.cpp（P2，任务 16）
// ============================================================================
#include "DynamicScenePass.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

DynamicScenePass::DynamicScenePass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void DynamicScenePass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    const GCaps& caps = device.GetCaps();
    if (!caps.supportsRayTracing || !caps.supportsRayQuery)
    {
        LOG_STREAM_WARN("DynamicScenePass")
            << "Device does not support Ray Query, example will only clear the screen.";
        return;
    }

    mWidth  = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowWidth());
    mHeight = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowHeight());
    if (mWidth  == 0) mWidth  = 1280;
    if (mHeight == 0) mHeight = 720;

    // 1) 单个三角形顶点缓冲（所有 instance 共享 → BLAS 去重）
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
        bd.debugName = "DynamicScenePass.TriangleVB";
        mVertexBuffer = device.CreateBuffer(bd);
    }

    // 2) AS 管理层：GetOrCreateBLAS 复用同一 BLAS；AddInstance 加入多个实例（去重验证）
    mASManager = std::make_unique<Mgr>(device);
    BLASGeometryDesc geo{};
    geo.vertexBuffer = mVertexBuffer;
    geo.vertexFormat = Format::R32G32B32_SFLOAT;
    geo.vertexStride = sizeof(float) * 3;
    geo.vertexCount  = 3;

    AccelerationStructureHandle blas;
    for (int i = 0; i < kInstanceCount; ++i)
    {
        // 三次调用返回同一 BLAS（去重）
        blas = mASManager->GetOrCreateBLAS(geo);
        if (!blas.IsValid())
        {
            LOG_STREAM_ERROR("DynamicScenePass") << "BLAS build failed, falling back to clear-only.";
            return;
        }
        TLASInstanceDesc inst{};
        inst.blas = blas;
        inst.mask = 0xFF;
        // 初始变换：缩放 + 沿 x 排布
        const float s = 0.30f;
        const float tx = (static_cast<float>(i) - 1.0f) * 0.7f;
        const float t[12] = {
            s, 0, 0, tx,
            0, s, 0, 0,
            0, 0, s, 0,
        };
        for (int k = 0; k < 12; ++k) inst.transform[k] = t[k];
        mInstances[i] = mASManager->AddInstance(inst);
    }
    // 首次构建 TLAS（拓扑变化 → 重建）
    mASManager->BuildOrRefit(nullptr);
    if (!mASManager->GetTLAS().IsValid())
    {
        LOG_STREAM_ERROR("DynamicScenePass") << "TLAS build failed, falling back to clear-only.";
        return;
    }

    // 3) storage image + sampler
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R8G8B8A8_UNORM;
        td.width = mWidth; td.height = mHeight;
        td.usage = TextureUsage::Storage | TextureUsage::Sampled;
        td.debugName = "DynamicScenePass.StorageImage";
        mStorageImage = device.CreateTexture(td);
    }
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "DynamicScenePass.Sampler";
        mSampler = device.CreateSampler(sd);
    }

    const std::string shaderDir = std::string(SOLUTION_DIR) + "Examples/Test_002_RayQueryHello/Shader/";

    // 4) compute（rayQuery，复用 raytrace.comp.glsl）
    {
        std::vector<uint8_t> csBytes;
        if (TitusAsset::ReadAllBytes(shaderDir + "raytrace.comp.glsl", csBytes))
        {
            ShaderDesc cs{};
            cs.stage = ShaderStage::Compute;
            cs.code = csBytes.data(); cs.bytes = csBytes.size();
            cs.debugName = "DynamicScenePass.CS";
            mComputeShader = device.CreateShader(cs);
        }
        if (mComputeShader.IsValid())
        {
            ComputePipelineDesc cpd{};
            cpd.computeShader = mComputeShader;
            cpd.debugName = "DynamicScenePass.ComputePipeline";
            ResourceBinding img{};
            img.name = "u_Output"; img.set = 0; img.binding = 0;
            img.type = ResourceBindingType::StorageTexture;
            img.stages = ShaderStage::Compute;
            cpd.resourceBindings.push_back(img);
            ResourceBinding tlas{};
            tlas.name = "u_TLAS"; tlas.set = 0; tlas.binding = 1;
            tlas.type = ResourceBindingType::AccelerationStructure;
            tlas.stages = ShaderStage::Compute;
            cpd.resourceBindings.push_back(tlas);
            mComputePipeline = device.CreatePipeline(cpd);
        }
    }

    // 5) 显示管线
    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (TitusAsset::ReadAllBytes(shaderDir + "blit.vert.glsl", vsBytes) &&
            TitusAsset::ReadAllBytes(shaderDir + "blit.frag.glsl", fsBytes))
        {
            ShaderDesc vs{};
            vs.stage = ShaderStage::Vertex; vs.code = vsBytes.data(); vs.bytes = vsBytes.size();
            vs.debugName = "DynamicScenePass.BlitVS";
            mBlitVS = device.CreateShader(vs);
            ShaderDesc fs{};
            fs.stage = ShaderStage::Fragment; fs.code = fsBytes.data(); fs.bytes = fsBytes.size();
            fs.debugName = "DynamicScenePass.BlitFS";
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
            gp.debugName = "DynamicScenePass.BlitPipeline";
            mBlitPipeline = device.CreatePipeline(gp);
        }
    }

    constexpr uint32_t LOCAL = 16;
    mGroupCountX = (mWidth  + LOCAL - 1) / LOCAL;
    mGroupCountY = (mHeight + LOCAL - 1) / LOCAL;

    mReady = mComputePipeline.IsValid() && mBlitPipeline.IsValid();
    if (mReady)
        LOG_STREAM_INFO("DynamicScenePass") << "Dynamic ray tracing example initialized ("
            << kInstanceCount << " instances reusing the same BLAS)";
}

void DynamicScenePass::Update(TitusRHI::IGDevice& /*device*/, uint32_t /*frameIndex*/)
{
    if (!mReady || !mASManager) return;

    // 每帧移动各 instance（仅 transform 变化 → 触发 refit 而非重建）
    mTime += 0.016f;
    for (int i = 0; i < kInstanceCount; ++i)
    {
        const float s  = 0.30f;
        const float ph = static_cast<float>(i) * 2.094f; // 2π/3 相位
        const float tx = 0.65f * std::sin(mTime + ph);
        const float ty = 0.45f * std::cos(mTime * 1.3f + ph);
        const float t[12] = {
            s, 0, 0, tx,
            0, s, 0, ty,
            0, 0, s, 0,
        };
        mASManager->SetInstanceTransform(mInstances[i], t);
    }
}

void DynamicScenePass::Destroy(TitusRHI::IGDevice& device)
{
    if (mBlitPipeline.IsValid())    device.Destroy(mBlitPipeline);
    if (mBlitFS.IsValid())          device.Destroy(mBlitFS);
    if (mBlitVS.IsValid())          device.Destroy(mBlitVS);
    if (mComputePipeline.IsValid()) device.Destroy(mComputePipeline);
    if (mComputeShader.IsValid())   device.Destroy(mComputeShader);
    if (mSampler.IsValid())         device.Destroy(mSampler);
    if (mStorageImage.IsValid())    device.Destroy(mStorageImage);
    // 管理层析构会释放其持有的 BLAS/TLAS
    mASManager.reset();
    if (mVertexBuffer.IsValid())    device.Destroy(mVertexBuffer);

    mBlitPipeline = {}; mBlitFS = {}; mBlitVS = {};
    mComputePipeline = {}; mComputeShader = {};
    mSampler = {}; mStorageImage = {}; mVertexBuffer = {};
}

void DynamicScenePass::Record(TitusRHI::IGDevice&        /*device*/,
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

    // 1) refit TLAS（transform 已在 Update 中更新 → 命令流内增量更新）
    AccelerationStructureHandle tlas = mASManager->BuildOrRefit(&cmd);

    // 2) 屏障：AS 构建写 → compute rayQuery 读
    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::AccelerationStructureBuild;
        bar.dstStage = PipelineStage::ComputeShader;
        bar.srcGlobalAccess = AccessFlags::AccelerationStructureWrite;
        bar.dstGlobalAccess = AccessFlags::AccelerationStructureRead;
        cmd.PipelineBarrier(bar);
    }

    // 3) compute：rayQuery 写 storage image
    cmd.BindPipeline(mComputePipeline);
    {
        ResourceSetDesc rs{};
        ResourceBindingValue img{};
        img.binding = 0;
        img.type = ResourceBindingType::StorageTexture;
        img.texture = mStorageImage;
        rs.bindings.push_back(img);
        ResourceBindingValue t{};
        t.binding = 1;
        t.type = ResourceBindingType::AccelerationStructure;
        t.accelStruct = tlas;
        rs.bindings.push_back(t);
        cmd.BindResourceSet(0, rs);
    }
    cmd.Dispatch(mGroupCountX, mGroupCountY, 1);

    // 4) 屏障：compute 写 → fragment 采样读
    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ComputeShader;
        bar.dstStage = PipelineStage::FragmentShader;
        bar.srcGlobalAccess = AccessFlags::ShaderWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    }

    // 5) 显示
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
