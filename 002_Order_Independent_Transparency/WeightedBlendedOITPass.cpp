// ============================================================================
// 002_Order_Independent_Transparency - WeightedBlendedOITPass.cpp
//
// Opaque Cornell → Accum (One One) → Revealage (Zero OneMinusSrcAlpha) →
// 全屏 Blend 到 backbuffer。
// ============================================================================
#include "WeightedBlendedOITPass.h"
#include "Scene.h"
#include "SceneDraw.h"
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
            LOG_STREAM_ERROR("WeightedBlendedOITPass") << "shader missing: " << path;
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

WeightedBlendedOITPass::WeightedBlendedOITPass()
{
    passEvent = TitusRHI::ERenderPassEvent::Transparent;
}

bool WeightedBlendedOITPass::CreateTargets(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    m_width = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    m_height = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (m_width == 0) m_width = 1920;
    if (m_height == 0) m_height = 1152;

    auto makeColor = [&](const char* name)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R16G16B16A16_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = name;
        return device.CreateTexture(td);
    };

    m_sceneColor = makeColor("WBOIT.SceneColor");
    m_accum = makeColor("WBOIT.Accum");
    m_revealage = makeColor("WBOIT.Revealage");

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::D32_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "WBOIT.Depth";
        m_depth = device.CreateTexture(td);
    }

    auto makeRT = [&](TextureHandle color, const char* name)
    {
        RenderTargetDesc rt{};
        rt.width = m_width;
        rt.height = m_height;
        rt.colorAttachments.push_back({color, 0, 0});
        rt.depthStencilAttachment = {m_depth, 0, 0};
        rt.debugName = name;
        return device.CreateRenderTarget(rt);
    };

    m_opaqueRT = makeRT(m_sceneColor, "WBOIT.OpaqueRT");
    m_accumRT = makeRT(m_accum, "WBOIT.AccumRT");
    m_revealRT = makeRT(m_revealage, "WBOIT.RevealRT");

    return m_sceneColor.IsValid() && m_accum.IsValid() && m_revealage.IsValid()
        && m_depth.IsValid()
        && m_opaqueRT.IsValid() && m_accumRT.IsValid() && m_revealRT.IsValid();
}

void WeightedBlendedOITPass::DestroyTargets(TitusRHI::IGDevice& device)
{
    if (m_revealRT.IsValid()) device.Destroy(m_revealRT);
    if (m_accumRT.IsValid()) device.Destroy(m_accumRT);
    if (m_opaqueRT.IsValid()) device.Destroy(m_opaqueRT);
    if (m_revealage.IsValid()) device.Destroy(m_revealage);
    if (m_accum.IsValid()) device.Destroy(m_accum);
    if (m_sceneColor.IsValid()) device.Destroy(m_sceneColor);
    if (m_depth.IsValid()) device.Destroy(m_depth);
    m_revealRT = {};
    m_accumRT = {};
    m_opaqueRT = {};
    m_revealage = {};
    m_accum = {};
    m_sceneColor = {};
    m_depth = {};
}

bool WeightedBlendedOITPass::CreateShaders(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    const std::string dir = std::string(SOLUTION_DIR) + kShaderDir;
    return LoadShader(device, ShaderStage::Vertex,   dir + "Scene_VS.glsl",          "WBOIT.SceneVS",   m_sceneVS)
        && LoadShader(device, ShaderStage::Fragment, dir + "Scene_FS.glsl",          "WBOIT.SceneFS",   m_sceneFS)
        && LoadShader(device, ShaderStage::Fragment, dir + "WBOIT_Accum_FS.glsl",    "WBOIT.AccumFS",   m_accumFS)
        && LoadShader(device, ShaderStage::Fragment, dir + "WBOIT_Revealage_FS.glsl","WBOIT.RevealFS",  m_revealFS)
        && LoadShader(device, ShaderStage::Vertex,   dir + "WBOIT_Blend_VS.glsl",    "WBOIT.BlendVS",   m_blendVS)
        && LoadShader(device, ShaderStage::Fragment, dir + "WBOIT_Blend_FS.glsl",    "WBOIT.BlendFS",   m_blendFS);
}

bool WeightedBlendedOITPass::CreatePipelines(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!m_scene) return false;

    const RenderTargetLayout offscreenLayout{
        {Format::R16G16B16A16_SFLOAT},
        Format::D32_SFLOAT,
        1
    };

    {
        GraphicsPipelineDesc pd{};
        FillGeometryPipelineShared(pd, m_sceneVS, m_sceneFS, m_scene->GetCornellHandle());
        pd.depthStencil.depthWriteEnable = true;
        pd.rtLayout = offscreenLayout;
        pd.debugName = "WBOIT.Opaque";
        m_opaquePipeline = device.CreatePipeline(pd);
    }
    {
        GraphicsPipelineDesc pd{};
        FillGeometryPipelineShared(pd, m_sceneVS, m_accumFS, m_scene->GetDragonHandle());
        pd.depthStencil.depthWriteEnable = false;
        pd.blend.attachments[0].blendEnable = true;
        pd.blend.attachments[0].srcColorBlendFactor = BlendFactor::One;
        pd.blend.attachments[0].dstColorBlendFactor = BlendFactor::One;
        pd.blend.attachments[0].srcAlphaBlendFactor = BlendFactor::One;
        pd.blend.attachments[0].dstAlphaBlendFactor = BlendFactor::One;
        pd.rtLayout = offscreenLayout;
        pd.debugName = "WBOIT.Accum";
        m_accumPipeline = device.CreatePipeline(pd);
    }
    {
        GraphicsPipelineDesc pd{};
        FillGeometryPipelineShared(pd, m_sceneVS, m_revealFS, m_scene->GetDragonHandle());
        pd.depthStencil.depthWriteEnable = false;
        pd.blend.attachments[0].blendEnable = true;
        pd.blend.attachments[0].srcColorBlendFactor = BlendFactor::Zero;
        pd.blend.attachments[0].dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
        pd.blend.attachments[0].srcAlphaBlendFactor = BlendFactor::Zero;
        pd.blend.attachments[0].dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha;
        pd.rtLayout = offscreenLayout;
        pd.debugName = "WBOIT.Revealage";
        m_revealPipeline = device.CreatePipeline(pd);
    }
    {
        GraphicsPipelineDesc pd{};
        pd.vertexShader = m_blendVS;
        pd.fragmentShader = m_blendFS;
        pd.topology = PrimitiveTopology::TriangleList;
        pd.rasterizer.cullMode = CullMode::None;
        pd.depthStencil.depthTestEnable = false;
        pd.depthStencil.depthWriteEnable = false;
        pd.blend.attachments.resize(1);
        pd.debugName = "WBOIT.Blend";

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
        addSampler("u_RevealageTex", 1);
        addSampler("u_SceneColorTex", 2);

        m_blendPipeline = device.CreatePipeline(pd);
    }

    return m_opaquePipeline.IsValid() && m_accumPipeline.IsValid()
        && m_revealPipeline.IsValid() && m_blendPipeline.IsValid();
}

void WeightedBlendedOITPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!CreateTargets(device) || !CreateShaders(device) || !CreatePipelines(device))
    {
        LOG_STREAM_ERROR("WeightedBlendedOITPass")
            << "init failed; WeightedBlended mode will draw nothing";
        return;
    }

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "WBOIT.Sampler";
        m_sampler = device.CreateSampler(sd);
    }

    BufferDesc bd{};
    bd.size = sizeof(SceneShadingUBO);
    bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
    bd.memory = MemoryUsage::CpuToGpu;
    bd.debugName = "WBOIT.UBO.Shading";
    m_shadingUbo = device.CreateBuffer(bd);
}

void WeightedBlendedOITPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_sampler.IsValid()) device.Destroy(m_sampler);
    if (m_blendPipeline.IsValid()) device.Destroy(m_blendPipeline);
    if (m_revealPipeline.IsValid()) device.Destroy(m_revealPipeline);
    if (m_accumPipeline.IsValid()) device.Destroy(m_accumPipeline);
    if (m_opaquePipeline.IsValid()) device.Destroy(m_opaquePipeline);
    if (m_blendFS.IsValid()) device.Destroy(m_blendFS);
    if (m_blendVS.IsValid()) device.Destroy(m_blendVS);
    if (m_revealFS.IsValid()) device.Destroy(m_revealFS);
    if (m_accumFS.IsValid()) device.Destroy(m_accumFS);
    if (m_sceneFS.IsValid()) device.Destroy(m_sceneFS);
    if (m_sceneVS.IsValid()) device.Destroy(m_sceneVS);
    m_shadingUbo = {};
    m_sampler = {};
    m_blendPipeline = {};
    m_revealPipeline = {};
    m_accumPipeline = {};
    m_opaquePipeline = {};
    m_blendFS = {};
    m_blendVS = {};
    m_revealFS = {};
    m_accumFS = {};
    m_sceneFS = {};
    m_sceneVS = {};
    DestroyTargets(device);
}

void WeightedBlendedOITPass::SetFullscreenViewport(TitusRHI::RenderCommandList& cmd) const
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

void WeightedBlendedOITPass::BindShading(TitusRHI::RenderCommandList& cmd) const
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

void WeightedBlendedOITPass::DrawDragons(TitusRHI::RenderCommandList& cmd) const
{
    if (!m_scene) return;
    const float opacity = m_ctx ? m_ctx->dragonOpacity : 0.40f;
    for (const auto& dragon : m_scene->GetDragons())
    {
        DrawModelColored(cmd, m_scene->GetDragonHandle(), dragon.modelMatrix,
                         &dragon.albedo, 1, opacity);
    }
}

void WeightedBlendedOITPass::Record(TitusRHI::IGDevice& device,
                                    TitusRHI::RenderCommandList& cmd,
                                    uint32_t /*frameIndex*/,
                                    uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;
    if (!m_ctx || m_ctx->mode != OITTechnique::WeightedBlended)
        return;
    if (!m_scene || !m_opaquePipeline.IsValid() || !m_accumPipeline.IsValid()
        || !m_revealPipeline.IsValid() || !m_blendPipeline.IsValid())
        return;

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("WBOIT::UpdateShading");
        SceneShadingUBO data{};
        FillSceneShadingUBO(data, m_ctx);
        device.UpdateBuffer(m_shadingUbo, &data, sizeof(data), 0);
    }

    // 1) Opaque Cornell → SceneColor + Depth
    {
        ZoneScopedN("WBOIT::Opaque");
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

    // 2) Accum：Blend One One，清空 0，测深度不写
    {
        ZoneScopedN("WBOIT::Accum");
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
        rp.depthStencilOp.storeOp = StoreOp::Store;

        cmd.BeginRenderPass(rp);
        SetFullscreenViewport(cmd);
        cmd.BindPipeline(m_accumPipeline);
        BindShading(cmd);
        DrawDragons(cmd);
        cmd.EndRenderPass();
    }

    // 3) Revealage：Blend Zero OneMinusSrcAlpha，清空 1
    {
        ZoneScopedN("WBOIT::Revealage");
        RenderPassBeginInfo rp{};
        rp.renderTarget = m_revealRT;
        RenderPassAttachmentOp colorOp{};
        colorOp.loadOp = LoadOp::Clear;
        colorOp.storeOp = StoreOp::Store;
        colorOp.clearValue.color[0] = 1.0f;
        colorOp.clearValue.color[1] = 1.0f;
        colorOp.clearValue.color[2] = 1.0f;
        colorOp.clearValue.color[3] = 1.0f;
        rp.colorOps.push_back(colorOp);
        rp.hasDepthStencil = true;
        rp.depthStencilOp.loadOp = LoadOp::Load;
        rp.depthStencilOp.storeOp = StoreOp::DontCare;

        cmd.BeginRenderPass(rp);
        SetFullscreenViewport(cmd);
        cmd.BindPipeline(m_revealPipeline);
        BindShading(cmd);
        DrawDragons(cmd);
        cmd.EndRenderPass();
    }

    // 4) 全屏 Blend → backbuffer
    {
        ZoneScopedN("WBOIT::Blend");
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
        cmd.BindPipeline(m_blendPipeline);

        ResourceSetDesc rs{};
        auto pushTex = [&](TextureHandle h, uint32_t binding)
        {
            ResourceBindingValue bv{};
            bv.binding = binding;
            bv.type = ResourceBindingType::CombinedImageSampler;
            bv.texture = h;
            bv.sampler = m_sampler;
            rs.bindings.push_back(bv);
        };
        pushTex(m_accum, 0);
        pushTex(m_revealage, 1);
        pushTex(m_sceneColor, 2);
        cmd.BindResourceSet(0, rs);
        cmd.Draw(3);
        cmd.EndRenderPass();
    }
}
