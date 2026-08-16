// ============================================================================
// 000_Forward_Deferred_ForwardPlus - ForwardPlusPass.cpp
//
// Clustered Forward：Depth → TileDepth 归约 → Compute 按 cluster 剔灯 →
// 复用预通道深度前向着色 → Resolve 回 backbuffer。
// ============================================================================
#include "ForwardPlusPass.h"
#include "Sponza.h"
#include "TechniqueContext.h"

#include <algorithm>
#include <cmath>
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
        TitusMath::Vec4 clusterZ{0.0f};     // x=near, y=far, z=zSlices, w=log(far/near)
    };
    static_assert(sizeof(CullParamsData) == 96, "CullParamsData std140 size");

    // 着色离屏 RT 的颜色格式；Resolve 之后才进 backbuffer。
    constexpr TitusRHI::Format kShadeColorFormat = TitusRHI::Format::R8G8B8A8_UNORM;

    uint64_t ClusterSSBOBytes(uint32_t tilesX, uint32_t tilesY)
    {
        return static_cast<uint64_t>(tilesX) * tilesY
            * static_cast<uint64_t>(ForwardPlusParams::Z_SLICES)
            * static_cast<uint64_t>(ForwardPlusParams::CLUSTER_STRIDE)
            * sizeof(uint32_t);
    }

    // 每 tile 两个 uint：min / max 视距的 IEEE754 位模式。
    uint64_t TileDepthSSBOBytes(uint32_t tilesX, uint32_t tilesY)
    {
        return static_cast<uint64_t>(tilesX) * tilesY * 2ull * sizeof(uint32_t);
    }

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
    //    D32 后面被着色通道以 LessOrEqual 复用，overdraw 因此降到 1。
    // ------------------------------------------------------------------
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R32_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.mipLevels = 1;
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
        td.mipLevels = 1;
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
    // 2) 着色离屏 RT：颜色新建，深度直接挂预通道那张 D32
    // ------------------------------------------------------------------
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kShadeColorFormat;
        td.width = m_width;
        td.height = m_height;
        td.mipLevels = 1;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = "ForwardPlus.ShadeColor";
        m_shadeColorTex = device.CreateTexture(td);
    }
    {
        RenderTargetDesc rt{};
        rt.width = m_width;
        rt.height = m_height;
        rt.colorAttachments.push_back({m_shadeColorTex, 0, 0});
        rt.depthStencilAttachment = {m_depthTex, 0, 0};
        rt.debugName = "ForwardPlus.ShadeRT";
        m_shadeRT = device.CreateRenderTarget(rt);
    }

    // ------------------------------------------------------------------
    // 3) UBO / SSBO
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
        BufferDesc bd{};
        bd.size = ClusterSSBOBytes(m_tilesX, m_tilesY);
        bd.usage = BufferUsage::StorageBuffer;
        bd.memory = MemoryUsage::GpuOnly;
        bd.debugName = "ForwardPlus.SSBO.TileLights";
        m_tileLightSSBO = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = TileDepthSSBOBytes(m_tilesX, m_tilesY);
        bd.usage = BufferUsage::StorageBuffer;
        bd.memory = MemoryUsage::GpuOnly;
        bd.debugName = "ForwardPlus.SSBO.TileDepth";
        m_tileDepthSSBO = device.CreateBuffer(bd);
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
    // 4) Depth pipeline
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
    // 5) TileDepth compute pipeline
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> csBytes;
        if (LoadShaderBytes(shaderDir + "ForwardPlusTileDepth_CS.glsl", csBytes))
        {
            ShaderDesc csDesc{};
            csDesc.stage = ShaderStage::Compute;
            csDesc.code = csBytes.data();
            csDesc.bytes = csBytes.size();
            csDesc.entryPoint = "main";
            csDesc.debugName = "ForwardPlusPass.TileDepthCS";
            m_tileDepthCS = device.CreateShader(csDesc);

            if (m_tileDepthCS.IsValid())
            {
                ComputePipelineDesc cpd{};
                cpd.computeShader = m_tileDepthCS;
                addCIS(cpd.resourceBindings, "u_DepthVS", 0, ShaderStage::Compute);
                addSSBO(cpd.resourceBindings, "u_TileDepthRange", 1, ShaderStage::Compute);
                addUBO(cpd.resourceBindings, "u_CullParams", 2, ShaderStage::Compute);
                cpd.debugName = "ForwardPlusPass.TileDepthPipeline";
                m_tileDepthPipeline = device.CreatePipeline(cpd);
            }
        }
        else
        {
            LOG_STREAM_ERROR("ForwardPlusPass") << "tile depth compute shader missing";
        }
    }

    // ------------------------------------------------------------------
    // 6) Cull compute pipeline
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
                addUBO(cpd.resourceBindings, "u_CullParams", 2, ShaderStage::Compute);
                addSSBO(cpd.resourceBindings, "u_TileDepthRange", 3, ShaderStage::Compute);
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
    // 7) Shade pipeline（离屏 RT，复用预通道深度做 Early-Z）
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
            // 深度已由预通道写好：只测不写，LessOrEqual 让最前面那层片元通过，
            // 被挡住的在 Early-Z 就被剔掉，overdraw 恒为 1。
            pd.depthStencil.depthTestEnable = true;
            pd.depthStencil.depthWriteEnable = false;
            pd.depthStencil.depthCompareOp = CompareOp::LessOrEqual;
            pd.blend.attachments.resize(1);
            pd.rtLayout.colorFormats = {kShadeColorFormat};
            pd.rtLayout.depthStencilFormat = Format::D32_SFLOAT;
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

    // ------------------------------------------------------------------
    // 8) Resolve pipeline（离屏颜色 -> backbuffer）
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "ForwardPlusResolve_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "ForwardPlusResolve_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "ForwardPlusPass.ResolveVS";
            m_resolveVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "ForwardPlusPass.ResolveFS";
            m_resolveFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_resolveVS;
            pd.fragmentShader = m_resolveFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.depthStencil.depthTestEnable = false;
            pd.depthStencil.depthWriteEnable = false;
            pd.blend.attachments.resize(1);
            // rtLayout.colorFormats 留空：VK 后端用 swapchain 默认 RenderPass；GL 忽略。

            addCIS(pd.resourceBindings, "u_ShadeColor", 0, ShaderStage::Fragment);
            pd.debugName = "ForwardPlusPass.ResolvePipeline";
            m_resolvePipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("ForwardPlusPass") << "resolve shaders missing";
        }
    }

    LOG_STREAM_INFO("ForwardPlusPass")
        << "tiles=" << m_tilesX << "x" << m_tilesY
        << " slices=" << ForwardPlusParams::Z_SLICES
        << " (" << m_width << "x" << m_height << ")"
        << " clusterSSBO=" << (ClusterSSBOBytes(m_tilesX, m_tilesY) >> 20) << "MB";
}

void ForwardPlusPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_resolvePipeline.IsValid()) device.Destroy(m_resolvePipeline);
    if (m_resolveFS.IsValid()) device.Destroy(m_resolveFS);
    if (m_resolveVS.IsValid()) device.Destroy(m_resolveVS);
    if (m_tileDepthSSBO.IsValid()) device.Destroy(m_tileDepthSSBO);
    if (m_tileLightSSBO.IsValid()) device.Destroy(m_tileLightSSBO);
    if (m_cullParamsUbo.IsValid()) device.Destroy(m_cullParamsUbo);
    if (m_lightUbo.IsValid()) device.Destroy(m_lightUbo);
    if (m_matricesUbo.IsValid()) device.Destroy(m_matricesUbo);
    if (m_shadePipeline.IsValid()) device.Destroy(m_shadePipeline);
    if (m_shadeFS.IsValid()) device.Destroy(m_shadeFS);
    if (m_shadeVS.IsValid()) device.Destroy(m_shadeVS);
    if (m_shadeRT.IsValid()) device.Destroy(m_shadeRT);
    if (m_shadeColorTex.IsValid()) device.Destroy(m_shadeColorTex);
    if (m_cullPipeline.IsValid()) device.Destroy(m_cullPipeline);
    if (m_cullCS.IsValid()) device.Destroy(m_cullCS);
    if (m_tileDepthPipeline.IsValid()) device.Destroy(m_tileDepthPipeline);
    if (m_tileDepthCS.IsValid()) device.Destroy(m_tileDepthCS);
    if (m_depthPipeline.IsValid()) device.Destroy(m_depthPipeline);
    if (m_depthFS.IsValid()) device.Destroy(m_depthFS);
    if (m_depthVS.IsValid()) device.Destroy(m_depthVS);
    if (m_depthSampler.IsValid()) device.Destroy(m_depthSampler);
    if (m_depthRT.IsValid()) device.Destroy(m_depthRT);
    if (m_depthTex.IsValid()) device.Destroy(m_depthTex);
    if (m_depthVSTex.IsValid()) device.Destroy(m_depthVSTex);
    m_resolvePipeline = {};
    m_resolveFS = {};
    m_resolveVS = {};
    m_tileDepthSSBO = {};
    m_tileLightSSBO = {};
    m_cullParamsUbo = {};
    m_lightUbo = {};
    m_matricesUbo = {};
    m_shadePipeline = {};
    m_shadeFS = {};
    m_shadeVS = {};
    m_shadeRT = {};
    m_shadeColorTex = {};
    m_cullPipeline = {};
    m_cullCS = {};
    m_tileDepthPipeline = {};
    m_tileDepthCS = {};
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
    if (!m_depthPipeline.IsValid() || !m_tileDepthPipeline.IsValid()
        || !m_cullPipeline.IsValid() || !m_shadePipeline.IsValid()
        || !m_resolvePipeline.IsValid())
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
            const auto camCfg = CAMERA::GetBuiltinFlyCameraConfig();
            const float nearZ = std::max(camCfg.nearPlane, 1e-4f);
            const float farZ = std::max(camCfg.farPlane, nearZ + 1e-3f);
            cp.clusterZ = TitusMath::Vec4(
                nearZ,
                farZ,
                static_cast<float>(ForwardPlusParams::Z_SLICES),
                std::log(farZ / nearZ));
            device.UpdateBuffer(m_cullParamsUbo, &cp, sizeof(cp), 0);
        }
    }

    RecordDepth(device, cmd);
    RecordTileDepth(device, cmd);
    RecordCull(device, cmd);
    RecordShade(device, cmd);
    RecordResolve(device, cmd);
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

    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ColorAttachment;
        bar.dstStage = PipelineStage::ComputeShader;
        bar.srcGlobalAccess = AccessFlags::ColorAttachmentWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        TextureBarrier tb{};
        tb.texture = m_depthVSTex;
        tb.oldLayout = TextureLayout::ColorAttachment;
        tb.newLayout = TextureLayout::ShaderReadOnly;
        tb.srcAccess = AccessFlags::ColorAttachmentWrite;
        tb.dstAccess = AccessFlags::ShaderRead;
        bar.textureBarriers.push_back(tb);
        cmd.PipelineBarrier(bar);
    }
}

void ForwardPlusPass::RecordTileDepth(TitusRHI::IGDevice& /*device*/,
                                      TitusRHI::RenderCommandList& cmd)
{
    using namespace TitusRHI;
    ZoneScopedN("ForwardPlus::TileDepth");

    cmd.BindPipeline(m_tileDepthPipeline);

    {
        ResourceSetDesc rs{};

        ResourceBindingValue depthVS{};
        depthVS.binding = 0;
        depthVS.type = ResourceBindingType::CombinedImageSampler;
        depthVS.texture = m_depthVSTex;
        depthVS.sampler = m_depthSampler;
        rs.bindings.push_back(depthVS);

        ResourceBindingValue tileDepth{};
        tileDepth.binding = 1;
        tileDepth.type = ResourceBindingType::StorageBuffer;
        tileDepth.buffer = m_tileDepthSSBO;
        tileDepth.bufferOffset = 0;
        tileDepth.bufferRange = TileDepthSSBOBytes(m_tilesX, m_tilesY);
        rs.bindings.push_back(tileDepth);

        ResourceBindingValue cull{};
        cull.binding = 2;
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
        bar.dstStage = PipelineStage::ComputeShader;
        bar.srcGlobalAccess = AccessFlags::ShaderWrite;
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
        ssbo.bufferRange = ClusterSSBOBytes(m_tilesX, m_tilesY);
        rs.bindings.push_back(ssbo);

        ResourceBindingValue cull{};
        cull.binding = 2;
        cull.type = ResourceBindingType::UniformBuffer;
        cull.buffer = m_cullParamsUbo;
        cull.bufferOffset = 0;
        cull.bufferRange = sizeof(CullParamsData);
        rs.bindings.push_back(cull);

        ResourceBindingValue tileDepth{};
        tileDepth.binding = 3;
        tileDepth.type = ResourceBindingType::StorageBuffer;
        tileDepth.buffer = m_tileDepthSSBO;
        tileDepth.bufferOffset = 0;
        tileDepth.bufferRange = TileDepthSSBOBytes(m_tilesX, m_tilesY);
        rs.bindings.push_back(tileDepth);

        cmd.BindResourceSet(0, rs);
    }

    // 一个 workgroup 一个 cluster：空 slice 在 shader 里立即返回。
    cmd.Dispatch(m_tilesX, m_tilesY, static_cast<uint32_t>(ForwardPlusParams::Z_SLICES));

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
    rp.renderTarget = m_shadeRT;
    rp.renderArea.width = m_width;
    rp.renderArea.height = m_height;

    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.02f;
    colorOp.clearValue.color[1] = 0.02f;
    colorOp.clearValue.color[2] = 0.03f;
    colorOp.clearValue.color[3] = 1.0f;
    rp.colorOps.push_back(colorOp);

    // 深度是预通道的产物，必须 Load —— 清掉就退化回全 overdraw 的 Forward。
    rp.hasDepthStencil = true;
    rp.depthStencilOp.loadOp = LoadOp::Load;
    rp.depthStencilOp.storeOp = StoreOp::DontCare;

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
        ssbo.bufferRange = ClusterSSBOBytes(m_tilesX, m_tilesY);
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

    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ColorAttachment;
        bar.dstStage = PipelineStage::FragmentShader;
        bar.srcGlobalAccess = AccessFlags::ColorAttachmentWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        TextureBarrier tb{};
        tb.texture = m_shadeColorTex;
        tb.oldLayout = TextureLayout::ColorAttachment;
        tb.newLayout = TextureLayout::ShaderReadOnly;
        tb.srcAccess = AccessFlags::ColorAttachmentWrite;
        tb.dstAccess = AccessFlags::ShaderRead;
        bar.textureBarriers.push_back(tb);
        cmd.PipelineBarrier(bar);
    }
}

void ForwardPlusPass::RecordResolve(TitusRHI::IGDevice& /*device*/,
                                    TitusRHI::RenderCommandList& cmd)
{
    using namespace TitusRHI;
    ZoneScopedN("ForwardPlus::Resolve");

    // 默认 backbuffer：renderTarget 留空
    RenderPassBeginInfo rp{};
    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.0f;
    colorOp.clearValue.color[1] = 0.0f;
    colorOp.clearValue.color[2] = 0.0f;
    colorOp.clearValue.color[3] = 1.0f;
    rp.colorOps.push_back(colorOp);
    rp.hasDepthStencil = true;
    rp.depthStencilOp.loadOp = LoadOp::Clear;
    rp.depthStencilOp.storeOp = StoreOp::DontCare;
    rp.depthStencilOp.clearValue.depth = 1.0f;
    rp.renderArea.width = 0;
    rp.renderArea.height = 0;

    cmd.BeginRenderPass(rp);

    const uint32_t vpW = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    const uint32_t vpH = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    Viewport vp{};
    vp.width = static_cast<float>(vpW);
    vp.height = static_cast<float>(vpH);
    cmd.SetViewport(vp);
    Rect2D sc{};
    sc.width = vpW;
    sc.height = vpH;
    cmd.SetScissor(sc);

    cmd.BindPipeline(m_resolvePipeline);

    {
        ResourceSetDesc rs{};
        ResourceBindingValue tex{};
        tex.binding = 0;
        tex.type = ResourceBindingType::CombinedImageSampler;
        tex.texture = m_shadeColorTex;
        tex.sampler = m_depthSampler;
        rs.bindings.push_back(tex);
        cmd.BindResourceSet(0, rs);
    }

    cmd.Draw(3);

    cmd.EndRenderPass();
}
