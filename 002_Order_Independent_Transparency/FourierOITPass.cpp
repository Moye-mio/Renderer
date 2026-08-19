// ============================================================================
// 002_Order_Independent_Transparency - FourierOITPass.cpp
//
// Opaque Cornell → Coefficient (MRT x2, One One) → Reconstruct (One One) →
// 全屏 Merge 到 backbuffer。
//
// 半透明几何要跑两遍完整的顶点/光栅化流程（先攒系数、再用系数重建），这是
// FOM 类算法的固有代价；换来的是显存恒定且完全不需要排序。
// ============================================================================
#include "FourierOITPass.h"
#include "Scene.h"
#include "SceneDraw.h"
#include "TechniqueContext.h"

#include <algorithm>
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
    const char* kShaderDir = "002_Order_Independent_Transparency/Shader/";

    bool LoadShader(TitusRHI::IGDevice& device,
                    TitusRHI::ShaderStage stage,
                    const std::string& path,
                    const char* debugName,
                    TitusRHI::ShaderHandle& out)
    {
        std::vector<uint8_t> bytes;
        if (!TitusAsset::ReadAllBytes(path, bytes))
        {
            LOG_STREAM_ERROR("FourierOITPass") << "shader missing: " << path;
            return false;
        }
        TitusRHI::ShaderDesc desc{};
        desc.stage = stage;
        desc.code = bytes.data();
        desc.bytes = bytes.size();
        desc.entryPoint = "main";
        desc.debugName = debugName;
        out = device.CreateShader(desc);
        return out.IsValid();
    }
}

FourierOITPass::FourierOITPass()
{
    passEvent = TitusRHI::ERenderPassEvent::Transparent;
}

bool FourierOITPass::CreateTargets(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    m_width = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    m_height = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (m_width == 0) m_width = 1920;
    if (m_height == 0) m_height = 1152;

    auto makeColor = [&](Format format, const char* name)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = format;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = name;
        return device.CreateTexture(td);
    };

    m_sceneColor = makeColor(Format::R16G16B16A16_SFLOAT, "FOIT.SceneColor");
    m_accum = makeColor(Format::R16G16B16A16_SFLOAT, "FOIT.Accum");
    // 系数是长距离加性累加的结果，半精度尾数不够，用 32F 保住高阶谐波。
    m_coefficientOne = makeColor(Format::R32G32B32A32_SFLOAT, "FOIT.CoefficientOne");
    m_coefficientTwo = makeColor(Format::R32G32B32A32_SFLOAT, "FOIT.CoefficientTwo");

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::D32_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "FOIT.Depth";
        m_depth = device.CreateTexture(td);
    }

    {
        RenderTargetDesc rt{};
        rt.width = m_width;
        rt.height = m_height;
        rt.colorAttachments.push_back({m_sceneColor, 0, 0});
        rt.depthStencilAttachment = {m_depth, 0, 0};
        rt.debugName = "FOIT.OpaqueRT";
        m_opaqueRT = device.CreateRenderTarget(rt);
    }
    {
        RenderTargetDesc rt{};
        rt.width = m_width;
        rt.height = m_height;
        rt.colorAttachments.push_back({m_coefficientOne, 0, 0});
        rt.colorAttachments.push_back({m_coefficientTwo, 0, 0});
        rt.depthStencilAttachment = {m_depth, 0, 0};
        rt.debugName = "FOIT.CoefficientRT";
        m_coefficientRT = device.CreateRenderTarget(rt);
    }
    {
        RenderTargetDesc rt{};
        rt.width = m_width;
        rt.height = m_height;
        rt.colorAttachments.push_back({m_accum, 0, 0});
        rt.depthStencilAttachment = {m_depth, 0, 0};
        rt.debugName = "FOIT.AccumRT";
        m_accumRT = device.CreateRenderTarget(rt);
    }

    return m_sceneColor.IsValid() && m_accum.IsValid()
        && m_coefficientOne.IsValid() && m_coefficientTwo.IsValid()
        && m_depth.IsValid()
        && m_opaqueRT.IsValid() && m_coefficientRT.IsValid() && m_accumRT.IsValid();
}

void FourierOITPass::DestroyTargets(TitusRHI::IGDevice& device)
{
    if (m_accumRT.IsValid()) device.Destroy(m_accumRT);
    if (m_coefficientRT.IsValid()) device.Destroy(m_coefficientRT);
    if (m_opaqueRT.IsValid()) device.Destroy(m_opaqueRT);
    if (m_coefficientTwo.IsValid()) device.Destroy(m_coefficientTwo);
    if (m_coefficientOne.IsValid()) device.Destroy(m_coefficientOne);
    if (m_accum.IsValid()) device.Destroy(m_accum);
    if (m_sceneColor.IsValid()) device.Destroy(m_sceneColor);
    if (m_depth.IsValid()) device.Destroy(m_depth);
    m_accumRT = {};
    m_coefficientRT = {};
    m_opaqueRT = {};
    m_coefficientTwo = {};
    m_coefficientOne = {};
    m_accum = {};
    m_sceneColor = {};
    m_depth = {};
}

bool FourierOITPass::CreateShaders(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    const std::string dir = std::string(SOLUTION_DIR) + kShaderDir;
    return LoadShader(device, ShaderStage::Vertex,   dir + "Scene_VS.glsl",                 "FOIT.SceneVS",       m_sceneVS)
        && LoadShader(device, ShaderStage::Fragment, dir + "Scene_FS.glsl",                 "FOIT.SceneFS",       m_sceneFS)
        && LoadShader(device, ShaderStage::Fragment, dir + "Fourier_Coefficient_FS.glsl",   "FOIT.CoefficientFS", m_coefficientFS)
        && LoadShader(device, ShaderStage::Fragment, dir + "Fourier_Reconstruct_FS.glsl",   "FOIT.ReconstructFS", m_reconstructFS)
        // 全屏三角形的 VS 与 WBOIT 通用，直接复用。
        && LoadShader(device, ShaderStage::Vertex,   dir + "WBOIT_Blend_VS.glsl",           "FOIT.MergeVS",       m_mergeVS)
        && LoadShader(device, ShaderStage::Fragment, dir + "Fourier_Merge_FS.glsl",         "FOIT.MergeFS",       m_mergeFS);
}

bool FourierOITPass::CreatePipelines(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!m_scene) return false;

    const RenderTargetLayout singleLayout{
        {Format::R16G16B16A16_SFLOAT},
        Format::D32_SFLOAT,
        1
    };
    const RenderTargetLayout coefficientLayout{
        {Format::R32G32B32A32_SFLOAT, Format::R32G32B32A32_SFLOAT},
        Format::D32_SFLOAT,
        1
    };

    {
        GraphicsPipelineDesc pd{};
        FillGeometryPipelineShared(pd, m_sceneVS, m_sceneFS, m_scene->GetCornellHandle());
        pd.depthStencil.depthWriteEnable = true;
        pd.rtLayout = singleLayout;
        pd.debugName = "FOIT.Opaque";
        m_opaquePipeline = device.CreatePipeline(pd);
    }
    {
        // 深度测试保留、深度写关闭：被不透明 Cornell 挡住的片元被剔除，
        // 而半透明片元之间不会互相剔除 —— 这是 OIT 必须的。
        GraphicsPipelineDesc pd{};
        FillGeometryPipelineShared(pd, m_sceneVS, m_coefficientFS, m_scene->GetDragonHandle());
        pd.depthStencil.depthWriteEnable = false;
        pd.blend.attachments.resize(2);
        for (auto& attachment : pd.blend.attachments)
        {
            attachment.blendEnable = true;
            attachment.srcColorBlendFactor = BlendFactor::One;
            attachment.dstColorBlendFactor = BlendFactor::One;
            attachment.srcAlphaBlendFactor = BlendFactor::One;
            attachment.dstAlphaBlendFactor = BlendFactor::One;
        }
        pd.rtLayout = coefficientLayout;
        pd.debugName = "FOIT.Coefficient";
        m_coefficientPipeline = device.CreatePipeline(pd);
    }
    {
        GraphicsPipelineDesc pd{};
        FillGeometryPipelineShared(pd, m_sceneVS, m_reconstructFS, m_scene->GetDragonHandle());
        pd.depthStencil.depthWriteEnable = false;
        pd.blend.attachments[0].blendEnable = true;
        pd.blend.attachments[0].srcColorBlendFactor = BlendFactor::One;
        pd.blend.attachments[0].dstColorBlendFactor = BlendFactor::One;
        pd.blend.attachments[0].srcAlphaBlendFactor = BlendFactor::One;
        pd.blend.attachments[0].dstAlphaBlendFactor = BlendFactor::One;
        pd.rtLayout = singleLayout;

        auto addSampler = [&](const char* name, uint32_t binding)
        {
            ResourceBinding rb{};
            rb.name = name;
            rb.set = 0;
            rb.binding = binding;
            rb.type = ResourceBindingType::CombinedImageSampler;
            rb.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rb);
        };
        addSampler("u_CoefficientOneTex", 1);
        addSampler("u_CoefficientTwoTex", 2);

        pd.debugName = "FOIT.Reconstruct";
        m_reconstructPipeline = device.CreatePipeline(pd);
    }
    {
        GraphicsPipelineDesc pd{};
        pd.vertexShader = m_mergeVS;
        pd.fragmentShader = m_mergeFS;
        pd.topology = PrimitiveTopology::TriangleList;
        pd.rasterizer.cullMode = CullMode::None;
        pd.depthStencil.depthTestEnable = false;
        pd.depthStencil.depthWriteEnable = false;
        pd.blend.attachments.resize(1);
        pd.debugName = "FOIT.Merge";

        auto addSampler = [&](const char* name, uint32_t binding)
        {
            ResourceBinding rb{};
            rb.name = name;
            rb.set = 0;
            rb.binding = binding;
            rb.type = ResourceBindingType::CombinedImageSampler;
            rb.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rb);
        };
        addSampler("u_AccumTex", 0);
        addSampler("u_CoefficientOneTex", 1);
        addSampler("u_SceneColorTex", 2);

        m_mergePipeline = device.CreatePipeline(pd);
    }

    return m_opaquePipeline.IsValid() && m_coefficientPipeline.IsValid()
        && m_reconstructPipeline.IsValid() && m_mergePipeline.IsValid();
}

void FourierOITPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!CreateTargets(device) || !CreateShaders(device) || !CreatePipelines(device))
    {
        LOG_STREAM_ERROR("FourierOITPass")
            << "init failed; Fourier mode will draw nothing";
        return;
    }

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "FOIT.Sampler";
        m_sampler = device.CreateSampler(sd);
    }

    BufferDesc bd{};
    bd.size = sizeof(SceneShadingUBO);
    bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
    bd.memory = MemoryUsage::CpuToGpu;
    bd.debugName = "FOIT.UBO.Shading";
    m_shadingUbo = device.CreateBuffer(bd);
}

void FourierOITPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_sampler.IsValid()) device.Destroy(m_sampler);
    if (m_mergePipeline.IsValid()) device.Destroy(m_mergePipeline);
    if (m_reconstructPipeline.IsValid()) device.Destroy(m_reconstructPipeline);
    if (m_coefficientPipeline.IsValid()) device.Destroy(m_coefficientPipeline);
    if (m_opaquePipeline.IsValid()) device.Destroy(m_opaquePipeline);
    if (m_mergeFS.IsValid()) device.Destroy(m_mergeFS);
    if (m_mergeVS.IsValid()) device.Destroy(m_mergeVS);
    if (m_reconstructFS.IsValid()) device.Destroy(m_reconstructFS);
    if (m_coefficientFS.IsValid()) device.Destroy(m_coefficientFS);
    if (m_sceneFS.IsValid()) device.Destroy(m_sceneFS);
    if (m_sceneVS.IsValid()) device.Destroy(m_sceneVS);
    m_shadingUbo = {};
    m_sampler = {};
    m_mergePipeline = {};
    m_reconstructPipeline = {};
    m_coefficientPipeline = {};
    m_opaquePipeline = {};
    m_mergeFS = {};
    m_mergeVS = {};
    m_reconstructFS = {};
    m_coefficientFS = {};
    m_sceneFS = {};
    m_sceneVS = {};
    DestroyTargets(device);
}

void FourierOITPass::SetFullscreenViewport(TitusRHI::RenderCommandList& cmd) const
{
    using namespace TitusRHI;
    Viewport vp{};
    vp.width = static_cast<float>(m_width);
    vp.height = static_cast<float>(m_height);
    cmd.SetViewport(vp);
    Rect2D sc{};
    sc.width = m_width;
    sc.height = m_height;
    cmd.SetScissor(sc);
}

void FourierOITPass::BindShading(TitusRHI::RenderCommandList& cmd) const
{
    using namespace TitusRHI;
    ResourceSetDesc rs{};
    ResourceBindingValue ubo{};
    ubo.binding = 0;
    ubo.type = ResourceBindingType::UniformBuffer;
    ubo.buffer = m_shadingUbo;
    ubo.bufferOffset = 0;
    ubo.bufferRange = sizeof(SceneShadingUBO);
    rs.bindings.push_back(ubo);
    cmd.BindResourceSet(0, rs);
}

void FourierOITPass::BindShadingWithCoefficients(TitusRHI::RenderCommandList& cmd) const
{
    using namespace TitusRHI;
    ResourceSetDesc rs{};

    ResourceBindingValue ubo{};
    ubo.binding = 0;
    ubo.type = ResourceBindingType::UniformBuffer;
    ubo.buffer = m_shadingUbo;
    ubo.bufferOffset = 0;
    ubo.bufferRange = sizeof(SceneShadingUBO);
    rs.bindings.push_back(ubo);

    auto pushTex = [&](TextureHandle texture, uint32_t binding)
    {
        ResourceBindingValue bv{};
        bv.binding = binding;
        bv.type = ResourceBindingType::CombinedImageSampler;
        bv.texture = texture;
        bv.sampler = m_sampler;
        rs.bindings.push_back(bv);
    };
    pushTex(m_coefficientOne, 1);
    pushTex(m_coefficientTwo, 2);

    cmd.BindResourceSet(0, rs);
}

// 系数累加与加权颜色累加都是纯加法，提交顺序不影响结果；这里照样走排序只是
// 为了让 UI 上的顺序开关对三种算法一致生效，从而直接观察到"换顺序画面不变"。
void FourierOITPass::DrawDragons(TitusRHI::RenderCommandList& cmd) const
{
    if (!m_scene) return;
    const float opacity = m_ctx ? m_ctx->dragonOpacity : 0.55f;
    const DragonDrawOrder order = m_ctx ? m_ctx->drawOrder : DragonDrawOrder::SceneOrder;
    const auto& dragons = m_scene->GetDragons();
    for (uint32_t i : BuildDragonDrawOrder(dragons, order))
    {
        DrawModelColored(cmd, m_scene->GetDragonHandle(), dragons[i].modelMatrix,
                         &dragons[i].albedo, 1, opacity);
    }
}

void FourierOITPass::Record(TitusRHI::IGDevice& device,
                            TitusRHI::RenderCommandList& cmd,
                            uint32_t /*frameIndex*/,
                            uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;
    if (!m_ctx || m_ctx->mode != OITTechnique::Fourier)
        return;
    if (!m_scene || !m_opaquePipeline.IsValid() || !m_coefficientPipeline.IsValid()
        || !m_reconstructPipeline.IsValid() || !m_mergePipeline.IsValid())
        return;

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("FOIT::UpdateShading");
        SceneShadingUBO data{};
        FillSceneShadingUBO(data, m_ctx);

        float zMin = 0.0f;
        float zMax = 1.0f;
        ComputeDragonViewDepthWindow(*m_scene, m_ctx->fourierDepthPad, zMin, zMax);
        const float invRange = (zMax > zMin) ? (1.0f / (zMax - zMin)) : 1.0f;
        const float harmonics = static_cast<float>(std::clamp(m_ctx->fourierHarmonics, 0, 3));
        data.fourierParams = TitusMath::Vec4(zMin, invRange, harmonics, 0.0f);

        device.UpdateBuffer(m_shadingUbo, &data, sizeof(data), 0);
    }

    // 1) Opaque Cornell → SceneColor + Depth
    {
        ZoneScopedN("FOIT::Opaque");
        RenderPassBeginInfo rp{};
        rp.renderTarget = m_opaqueRT;
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
        rp.depthStencilOp.storeOp = StoreOp::Store;
        rp.depthStencilOp.clearValue.depth = 1.0f;
        rp.depthStencilOp.clearValue.stencil = 0;

        cmd.BeginRenderPass(rp);
        SetFullscreenViewport(cmd);
        cmd.BindPipeline(m_opaquePipeline);
        BindShading(cmd);
        const auto& cornellAlbedo = m_scene->GetCornellAlbedo();
        DrawModelColored(cmd, m_scene->GetCornellHandle(), m_scene->GetCornellMatrix(),
                         cornellAlbedo.data(), cornellAlbedo.size(), 1.0f);
        cmd.EndRenderPass();
    }

    // 2) Coefficient：两张 MRT 各自 One One 累加，清空 0
    {
        ZoneScopedN("FOIT::Coefficient");
        RenderPassBeginInfo rp{};
        rp.renderTarget = m_coefficientRT;
        RenderPassAttachmentOp colorOp{};
        colorOp.loadOp = LoadOp::Clear;
        colorOp.storeOp = StoreOp::Store;
        colorOp.clearValue.color[0] = 0.0f;
        colorOp.clearValue.color[1] = 0.0f;
        colorOp.clearValue.color[2] = 0.0f;
        colorOp.clearValue.color[3] = 0.0f;
        rp.colorOps.push_back(colorOp);
        rp.colorOps.push_back(colorOp);
        rp.hasDepthStencil = true;
        rp.depthStencilOp.loadOp = LoadOp::Load;
        rp.depthStencilOp.storeOp = StoreOp::Store;

        cmd.BeginRenderPass(rp);
        SetFullscreenViewport(cmd);
        cmd.BindPipeline(m_coefficientPipeline);
        BindShading(cmd);
        DrawDragons(cmd);
        cmd.EndRenderPass();
    }

    // 3) Reconstruct：读系数解析求透射率，加权颜色 One One 累加，清空 0
    {
        ZoneScopedN("FOIT::Reconstruct");
        RenderPassBeginInfo rp{};
        rp.renderTarget = m_accumRT;
        RenderPassAttachmentOp colorOp{};
        colorOp.loadOp = LoadOp::Clear;
        colorOp.storeOp = StoreOp::Store;
        colorOp.clearValue.color[0] = 0.0f;
        colorOp.clearValue.color[1] = 0.0f;
        colorOp.clearValue.color[2] = 0.0f;
        colorOp.clearValue.color[3] = 0.0f;
        rp.colorOps.push_back(colorOp);
        rp.hasDepthStencil = true;
        rp.depthStencilOp.loadOp = LoadOp::Load;
        rp.depthStencilOp.storeOp = StoreOp::DontCare;

        cmd.BeginRenderPass(rp);
        SetFullscreenViewport(cmd);
        cmd.BindPipeline(m_reconstructPipeline);
        BindShadingWithCoefficients(cmd);
        DrawDragons(cmd);
        cmd.EndRenderPass();
    }

    // 4) 全屏 Merge → backbuffer
    {
        ZoneScopedN("FOIT::Merge");
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
        rp.depthStencilOp.loadOp = LoadOp::DontCare;
        rp.depthStencilOp.storeOp = StoreOp::DontCare;
        rp.depthStencilOp.clearValue.depth = 1.0f;

        cmd.BeginRenderPass(rp);
        SetFullscreenViewport(cmd);
        cmd.BindPipeline(m_mergePipeline);

        ResourceSetDesc rs{};
        auto pushTex = [&](TextureHandle texture, uint32_t binding)
        {
            ResourceBindingValue bv{};
            bv.binding = binding;
            bv.type = ResourceBindingType::CombinedImageSampler;
            bv.texture = texture;
            bv.sampler = m_sampler;
            rs.bindings.push_back(bv);
        };
        pushTex(m_accum, 0);
        pushTex(m_coefficientOne, 1);
        pushTex(m_sceneColor, 2);
        cmd.BindResourceSet(0, rs);
        cmd.Draw(3);
        cmd.EndRenderPass();
    }
}
