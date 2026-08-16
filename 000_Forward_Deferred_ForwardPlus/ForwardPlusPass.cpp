// ============================================================================
// 000_Forward_Deferred_ForwardPlus - ForwardPlusPass.cpp
//
// Forward+：Depth(R32F 视空间 Z) → Compute 分块剔灯 → 按 tile 灯表前向着色。
// ============================================================================
#include "ForwardPlusPass.h"
#include "Sponza.h"
#include "TechniqueContext.h"

#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"
#include "TracySupport.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

namespace
{
    struct CullParamsData
    {
        TitusMath::Mat4 invProj{1.0f};
        TitusMath::IVec4 screenAndTiles{0}; // x=width, y=height, z=tilesX, w=tilesY
    };
    static_assert(sizeof(CullParamsData) == 80, "CullParamsData std140 size");

    bool LoadShaderBytes(const std::string& path, std::vector<uint8_t>& out)
    {
        return TitusAsset::ReadAllBytes(path, out);
    }
}

ForwardPlusPass::ForwardPlusPass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void ForwardPlusPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    m_width = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    m_height = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (m_width == 0) m_width = 1920;
    if (m_height == 0) m_height = 1152;

    m_tilesX = (m_width + ForwardPlusParams::TILE_SIZE - 1u) / ForwardPlusParams::TILE_SIZE;
    m_tilesY = (m_height + ForwardPlusParams::TILE_SIZE - 1u) / ForwardPlusParams::TILE_SIZE;

    const std::string shaderDir = std::string(SOLUTION_DIR) + "000_Forward_Deferred_ForwardPlus/Shader/";

    // ------------------------------------------------------------------
    // 1) Depth 预通道 RT：R32F 视空间 Z（Sampled）+ D32
    // ------------------------------------------------------------------
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R32_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = "ForwardPlus.DepthVS";
        m_depthVSTex = device.CreateTexture(td);
    }
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::D32_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "ForwardPlus.Depth";
        m_depthTex = device.CreateTexture(td);
    }
    {
        RenderTargetDesc rt{};
        rt.width = m_width;
        rt.height = m_height;
        rt.colorAttachments.push_back({m_depthVSTex, 0, 0});
        rt.depthStencilAttachment = {m_depthTex, 0, 0};
        rt.debugName = "ForwardPlus.DepthRT";
        m_depthRT = device.CreateRenderTarget(rt);
    }
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "ForwardPlus.DepthSampler";
        m_depthSampler = device.CreateSampler(sd);
    }

    // ------------------------------------------------------------------
    // 2) UBO / SSBO
    // ------------------------------------------------------------------
    {
        BufferDesc bd{};
        bd.size = sizeof(TitusMath::Mat4) * 2;
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "ForwardPlus.UBO.Matrices";
        m_matricesUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(SharedShadingParams::LightBlockData);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "ForwardPlus.UBO.Lights";
        m_lightUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(CullParamsData);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "ForwardPlus.UBO.CullParams";
        m_cullParamsUbo = device.CreateBuffer(bd);
    }
    {
        const uint64_t ssboBytes = static_cast<uint64_t>(m_tilesX) * m_tilesY
            * ForwardPlusParams::TILE_STRIDE * sizeof(uint32_t);
        BufferDesc bd{};
        bd.size = ssboBytes;
        bd.usage = BufferUsage::StorageBuffer;
        bd.memory = MemoryUsage::GpuOnly;
        bd.debugName = "ForwardPlus.SSBO.TileLights";
        m_tileLightSSBO = device.CreateBuffer(bd);
    }

    auto addUBO = [](std::vector<ResourceBinding>& dst, const char* name,
                     uint32_t binding, ShaderStage stages)
    {
        ResourceBinding rb{};
        rb.name = name;
        rb.set = 0;
        rb.binding = binding;
        rb.type = ResourceBindingType::UniformBuffer;
        rb.stages = stages;
        dst.push_back(rb);
    };
    auto addSSBO = [](std::vector<ResourceBinding>& dst, const char* name,
                      uint32_t binding, ShaderStage stages)
    {
        ResourceBinding rb{};
        rb.name = name;
        rb.set = 0;
        rb.binding = binding;
        rb.type = ResourceBindingType::StorageBuffer;
        rb.stages = stages;
        dst.push_back(rb);
    };
    auto addCIS = [](std::vector<ResourceBinding>& dst, const char* name,
                     uint32_t binding, ShaderStage stages)
    {
        ResourceBinding rb{};
        rb.name = name;
        rb.set = 0;
        rb.binding = binding;
        rb.type = ResourceBindingType::CombinedImageSampler;
        rb.stages = stages;
        dst.push_back(rb);
    };

    // ------------------------------------------------------------------
    // 3) Depth pipeline
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "ForwardPlusDepth_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "ForwardPlusDepth_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "ForwardPlusPass.DepthVS";
            m_depthVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "ForwardPlusPass.DepthFS";
            m_depthFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_depthVS;
            pd.fragmentShader = m_depthFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.rasterizer.frontFace = FrontFace::CounterClockwise;
            pd.depthStencil.depthTestEnable = true;
            pd.depthStencil.depthWriteEnable = true;
            pd.depthStencil.depthCompareOp = CompareOp::Less;
            pd.blend.attachments.resize(1);
            pd.rtLayout.colorFormats = {Format::R32_SFLOAT};
            pd.rtLayout.depthStencilFormat = Format::D32_SFLOAT;
            if (m_sponza)
                pd.vertexLayout = GetMeshSharedLayout(m_sponza->GetModelHandle());

            PushConstantRange pcModel{};
            pcModel.stages = ShaderStage::Vertex;
            pcModel.offset = 0;
            pcModel.size = sizeof(TitusMath::Mat4);
            pcModel.glName = "u_ModelMatrix";
            pd.pushConstantRanges.push_back(pcModel);

            addUBO(pd.resourceBindings, "u_Matrices4ProjectionWorld", 0, ShaderStage::Vertex);
            pd.debugName = "ForwardPlusPass.DepthPipeline";
            m_depthPipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("ForwardPlusPass") << "depth shaders missing";
        }
    }

    // ------------------------------------------------------------------
    // 4) Cull compute pipeline
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> csBytes;
        if (LoadShaderBytes(shaderDir + "ForwardPlusCull_CS.glsl", csBytes))
        {
            ShaderDesc csDesc{};
            csDesc.stage = ShaderStage::Compute;
            csDesc.code = csBytes.data();
            csDesc.bytes = csBytes.size();
            csDesc.entryPoint = "main";
            csDesc.debugName = "ForwardPlusPass.CullCS";
            m_cullCS = device.CreateShader(csDesc);

            if (m_cullCS.IsValid())
            {
                ComputePipelineDesc cpd{};
                cpd.computeShader = m_cullCS;
                addUBO(cpd.resourceBindings, "u_LightBlock", 0, ShaderStage::Compute);
                addSSBO(cpd.resourceBindings, "u_TileLightList", 1, ShaderStage::Compute);
                addCIS(cpd.resourceBindings, "u_DepthVS", 2, ShaderStage::Compute);
                addUBO(cpd.resourceBindings, "u_CullParams", 3, ShaderStage::Compute);
                cpd.debugName = "ForwardPlusPass.CullPipeline";
                m_cullPipeline = device.CreatePipeline(cpd);
            }
        }
        else
        {
            LOG_STREAM_ERROR("ForwardPlusPass") << "cull compute shader missing";
        }
    }

    // ------------------------------------------------------------------
    // 5) Shade pipeline（默认 backbuffer）
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "ForwardPlus_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "ForwardPlus_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "ForwardPlusPass.ShadeVS";
            m_shadeVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "ForwardPlusPass.ShadeFS";
            m_shadeFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_shadeVS;
            pd.fragmentShader = m_shadeFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.rasterizer.frontFace = FrontFace::CounterClockwise;
            pd.depthStencil.depthTestEnable = true;
            pd.depthStencil.depthWriteEnable = true;
            pd.depthStencil.depthCompareOp = CompareOp::Less;
            pd.blend.attachments.resize(1);
            if (m_sponza)
                pd.vertexLayout = GetMeshSharedLayout(m_sponza->GetModelHandle());

            PushConstantRange pcModel{};
            pcModel.stages = ShaderStage::Vertex;
            pcModel.offset = 0;
            pcModel.size = sizeof(TitusMath::Mat4);
            pcModel.glName = "u_ModelMatrix";
            pd.pushConstantRanges.push_back(pcModel);

            PushConstantRange pcDebug{};
            pcDebug.stages = ShaderStage::Fragment;
            pcDebug.offset = 64;
            pcDebug.size = sizeof(int32_t);
            pcDebug.glName = "u_DebugView";
            pd.pushConstantRanges.push_back(pcDebug);

            addUBO(pd.resourceBindings, "u_Matrices4ProjectionWorld", 0, ShaderStage::Vertex);
            addCIS(pd.resourceBindings, "u_DiffuseTexture", 1, ShaderStage::Fragment);
            addUBO(pd.resourceBindings, "u_LightBlock", 2, ShaderStage::Fragment);
            addSSBO(pd.resourceBindings, "u_TileLightList", 3, ShaderStage::Fragment);
            addUBO(pd.resourceBindings, "u_CullParams", 4, ShaderStage::Fragment);
            pd.debugName = "ForwardPlusPass.ShadePipeline";
            m_shadePipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("ForwardPlusPass") << "shade shaders missing";
        }
    }

    LOG_STREAM_INFO("ForwardPlusPass")
        << "tiles=" << m_tilesX << "x" << m_tilesY
        << " (" << m_width << "x" << m_height << ")";
}

void ForwardPlusPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_tileLightSSBO.IsValid()) device.Destroy(m_tileLightSSBO);
    if (m_cullParamsUbo.IsValid()) device.Destroy(m_cullParamsUbo);
    if (m_lightUbo.IsValid()) device.Destroy(m_lightUbo);
    if (m_matricesUbo.IsValid()) device.Destroy(m_matricesUbo);
    if (m_shadePipeline.IsValid()) device.Destroy(m_shadePipeline);
    if (m_shadeFS.IsValid()) device.Destroy(m_shadeFS);
    if (m_shadeVS.IsValid()) device.Destroy(m_shadeVS);
    if (m_cullPipeline.IsValid()) device.Destroy(m_cullPipeline);
    if (m_cullCS.IsValid()) device.Destroy(m_cullCS);
    if (m_depthPipeline.IsValid()) device.Destroy(m_depthPipeline);
    if (m_depthFS.IsValid()) device.Destroy(m_depthFS);
    if (m_depthVS.IsValid()) device.Destroy(m_depthVS);
    if (m_depthSampler.IsValid()) device.Destroy(m_depthSampler);
    if (m_depthRT.IsValid()) device.Destroy(m_depthRT);
    if (m_depthTex.IsValid()) device.Destroy(m_depthTex);
    if (m_depthVSTex.IsValid()) device.Destroy(m_depthVSTex);
    m_tileLightSSBO = {};
    m_cullParamsUbo = {};
    m_lightUbo = {};
    m_matricesUbo = {};
    m_shadePipeline = {};
    m_shadeFS = {};
    m_shadeVS = {};
    m_cullPipeline = {};
    m_cullCS = {};
    m_depthPipeline = {};
    m_depthFS = {};
    m_depthVS = {};
    m_depthSampler = {};
    m_depthRT = {};
    m_depthTex = {};
    m_depthVSTex = {};
}

void ForwardPlusPass::Record(TitusRHI::IGDevice& device,
                             TitusRHI::RenderCommandList& cmd,
                             uint32_t /*frameIndex*/,
                             uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_ctx || m_ctx->mode != ShadingTechnique::ForwardPlus)
        return;
    if (!m_depthPipeline.IsValid() || !m_cullPipeline.IsValid() || !m_shadePipeline.IsValid())
        return;

    {
        ZoneScopedN("ForwardPlus::UpdateUBOs");
        if (m_matricesUbo.IsValid())
        {
            TitusMath::Mat4 mats[2] = {
                CAMERA::GetMainCameraProjectionMatrix(),
                CAMERA::GetMainCameraViewMatrix()
            };
            device.UpdateBuffer(m_matricesUbo, mats, sizeof(mats), 0);
        }
        if (m_lightUbo.IsValid())
        {
            SharedShadingParams::LightBlockData data{};
            m_ctx->shared.FillLightBlock(data, CAMERA::GetMainCameraViewMatrix());
            device.UpdateBuffer(m_lightUbo, &data, sizeof(data), 0);
        }
        if (m_cullParamsUbo.IsValid())
        {
            CullParamsData cp{};
            cp.invProj = TitusMath::inverse(CAMERA::GetMainCameraProjectionMatrix());
            cp.screenAndTiles = TitusMath::IVec4(
                static_cast<int>(m_width),
                static_cast<int>(m_height),
                static_cast<int>(m_tilesX),
                static_cast<int>(m_tilesY));
            device.UpdateBuffer(m_cullParamsUbo, &cp, sizeof(cp), 0);
        }
    }

    RecordDepth(device, cmd);
    RecordCull(device, cmd);
    RecordShade(device, cmd);
}

void ForwardPlusPass::RecordDepth(TitusRHI::IGDevice& /*device*/,
                                  TitusRHI::RenderCommandList& cmd)
{
    using namespace TitusRHI;
    ZoneScopedN("ForwardPlus::Depth");

    RenderPassBeginInfo rp{};
    rp.renderTarget = m_depthRT;
    rp.renderArea.width = m_width;
    rp.renderArea.height = m_height;

    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.0f;
    rp.colorOps.push_back(colorOp);

    rp.hasDepthStencil = true;
    rp.depthStencilOp.loadOp = LoadOp::Clear;
    rp.depthStencilOp.storeOp = StoreOp::Store;
    rp.depthStencilOp.clearValue.depth = 1.0f;

    cmd.BeginRenderPass(rp);

    Viewport vp{};
    vp.width = static_cast<float>(m_width);
    vp.height = static_cast<float>(m_height);
    cmd.SetViewport(vp);
    Rect2D sc{};
    sc.width = m_width;
    sc.height = m_height;
    cmd.SetScissor(sc);

    cmd.BindPipeline(m_depthPipeline);

    {
        ResourceSetDesc rs{};
        ResourceBindingValue mats{};
        mats.binding = 0;
        mats.type = ResourceBindingType::UniformBuffer;
        mats.buffer = m_matricesUbo;
        mats.bufferOffset = 0;
        mats.bufferRange = sizeof(TitusMath::Mat4) * 2;
        rs.bindings.push_back(mats);
        cmd.BindResourceSet(0, rs);
    }

    if (m_sponza)
    {
        const TitusMath::Mat4 model = m_sponza->GetModelMatrix();
        cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);
        DrawGpuModel(cmd, m_sponza->GetModelHandle());
    }

    cmd.EndRenderPass();

    // Color RT 写完 → Compute 采样
    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ColorAttachment;
        bar.dstStage = PipelineStage::ComputeShader;
        bar.srcGlobalAccess = AccessFlags::ColorAttachmentWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    }
}

void ForwardPlusPass::RecordCull(TitusRHI::IGDevice& /*device*/,
                                 TitusRHI::RenderCommandList& cmd)
{
    using namespace TitusRHI;
    ZoneScopedN("ForwardPlus::Cull");

    cmd.BindPipeline(m_cullPipeline);

    {
        ResourceSetDesc rs{};

        ResourceBindingValue lights{};
        lights.binding = 0;
        lights.type = ResourceBindingType::UniformBuffer;
        lights.buffer = m_lightUbo;
        lights.bufferOffset = 0;
        lights.bufferRange = sizeof(SharedShadingParams::LightBlockData);
        rs.bindings.push_back(lights);

        ResourceBindingValue ssbo{};
        ssbo.binding = 1;
        ssbo.type = ResourceBindingType::StorageBuffer;
        ssbo.buffer = m_tileLightSSBO;
        ssbo.bufferOffset = 0;
        ssbo.bufferRange = static_cast<uint64_t>(m_tilesX) * m_tilesY
            * ForwardPlusParams::TILE_STRIDE * sizeof(uint32_t);
        rs.bindings.push_back(ssbo);

        ResourceBindingValue depth{};
        depth.binding = 2;
        depth.type = ResourceBindingType::CombinedImageSampler;
        depth.texture = m_depthVSTex;
        depth.sampler = m_depthSampler;
        rs.bindings.push_back(depth);

        ResourceBindingValue cull{};
        cull.binding = 3;
        cull.type = ResourceBindingType::UniformBuffer;
        cull.buffer = m_cullParamsUbo;
        cull.bufferOffset = 0;
        cull.bufferRange = sizeof(CullParamsData);
        rs.bindings.push_back(cull);

        cmd.BindResourceSet(0, rs);
    }

    cmd.Dispatch(m_tilesX, m_tilesY, 1);

    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ComputeShader;
        bar.dstStage = PipelineStage::FragmentShader;
        bar.srcGlobalAccess = AccessFlags::ShaderWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    }
}

void ForwardPlusPass::RecordShade(TitusRHI::IGDevice& /*device*/,
                                  TitusRHI::RenderCommandList& cmd)
{
    using namespace TitusRHI;
    ZoneScopedN("ForwardPlus::Shade");

    RenderPassBeginInfo rp{};
    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.02f;
    colorOp.clearValue.color[1] = 0.02f;
    colorOp.clearValue.color[2] = 0.03f;
    colorOp.clearValue.color[3] = 1.0f;
    rp.colorOps.push_back(colorOp);
    rp.hasDepthStencil = true;
    rp.depthStencilOp.loadOp = LoadOp::Clear;
    rp.depthStencilOp.storeOp = StoreOp::DontCare;
    rp.depthStencilOp.clearValue.depth = 1.0f;
    rp.renderArea.width = 0;
    rp.renderArea.height = 0;

    cmd.BeginRenderPass(rp);

    Viewport vp{};
    vp.width = static_cast<float>(m_width);
    vp.height = static_cast<float>(m_height);
    cmd.SetViewport(vp);
    Rect2D sc{};
    sc.width = m_width;
    sc.height = m_height;
    cmd.SetScissor(sc);

    cmd.BindPipeline(m_shadePipeline);

    {
        ResourceSetDesc rs{};

        ResourceBindingValue mats{};
        mats.binding = 0;
        mats.type = ResourceBindingType::UniformBuffer;
        mats.buffer = m_matricesUbo;
        mats.bufferOffset = 0;
        mats.bufferRange = sizeof(TitusMath::Mat4) * 2;
        rs.bindings.push_back(mats);

        ResourceBindingValue lights{};
        lights.binding = 2;
        lights.type = ResourceBindingType::UniformBuffer;
        lights.buffer = m_lightUbo;
        lights.bufferOffset = 0;
        lights.bufferRange = sizeof(SharedShadingParams::LightBlockData);
        rs.bindings.push_back(lights);

        ResourceBindingValue ssbo{};
        ssbo.binding = 3;
        ssbo.type = ResourceBindingType::StorageBuffer;
        ssbo.buffer = m_tileLightSSBO;
        ssbo.bufferOffset = 0;
        ssbo.bufferRange = static_cast<uint64_t>(m_tilesX) * m_tilesY
            * ForwardPlusParams::TILE_STRIDE * sizeof(uint32_t);
        rs.bindings.push_back(ssbo);

        ResourceBindingValue cull{};
        cull.binding = 4;
        cull.type = ResourceBindingType::UniformBuffer;
        cull.buffer = m_cullParamsUbo;
        cull.bufferOffset = 0;
        cull.bufferRange = sizeof(CullParamsData);
        rs.bindings.push_back(cull);

        cmd.BindResourceSet(0, rs);
    }

    const int32_t debugView = static_cast<int32_t>(m_ctx->forwardPlus.debugView);
    cmd.PushConstants(ShaderStage::Fragment, 64, sizeof(int32_t), &debugView);

    if (m_sponza)
    {
        const TitusMath::Mat4 model = m_sponza->GetModelMatrix();
        cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);
        DrawGpuModelWithDiffuse(cmd, m_sponza->GetModelHandle(), 0, 1);
    }

    cmd.EndRenderPass();
}
