// ============================================================================
// 002_Order_Independent_Transparency - ScenePass.cpp
//
// Baseline：不透明 Cornell → 朴素 Alpha 半透明 Dragon，画到默认 backbuffer。
// mode != Baseline 时早退，由 WeightedBlendedOITPass 接管。
// ============================================================================
#include "ScenePass.h"
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

ScenePass::ScenePass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

bool ScenePass::CreateShaders(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    const std::string shaderDir = std::string(SOLUTION_DIR) + "002_Order_Independent_Transparency/Shader/";
    std::vector<uint8_t> vsBytes, fsBytes;
    if (!TitusAsset::ReadAllBytes(shaderDir + "Scene_VS.glsl", vsBytes) ||
        !TitusAsset::ReadAllBytes(shaderDir + "Scene_FS.glsl", fsBytes))
    {
        LOG_STREAM_ERROR("ScenePass") << "shader files missing; pipeline not created";
        return false;
    }

    ShaderDesc vsDesc{};
    vsDesc.stage = ShaderStage::Vertex;
    vsDesc.code = vsBytes.data();
    vsDesc.bytes = vsBytes.size();
    vsDesc.entryPoint = "main";
    vsDesc.debugName = "ScenePass.VS";
    m_vs = device.CreateShader(vsDesc);

    ShaderDesc fsDesc{};
    fsDesc.stage = ShaderStage::Fragment;
    fsDesc.code = fsBytes.data();
    fsDesc.bytes = fsBytes.size();
    fsDesc.entryPoint = "main";
    fsDesc.debugName = "ScenePass.FS";
    m_fs = device.CreateShader(fsDesc);
    return m_vs.IsValid() && m_fs.IsValid();
}

bool ScenePass::CreatePipelines(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!m_scene) return false;

    {
        GraphicsPipelineDesc pd{};
        FillGeometryPipelineShared(pd, m_vs, m_fs, m_scene->GetCornellHandle());
        pd.depthStencil.depthWriteEnable = true;
        pd.debugName = "ScenePass.Opaque";
        m_opaquePipeline = device.CreatePipeline(pd);
    }
    {
        GraphicsPipelineDesc pd{};
        FillGeometryPipelineShared(pd, m_vs, m_fs, m_scene->GetDragonHandle());
        pd.depthStencil.depthWriteEnable = false;
        pd.blend.attachments[0].blendEnable = true;
        pd.blend.attachments[0].srcColorBlendFactor = BlendFactor::SrcAlpha;
        pd.blend.attachments[0].dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
        pd.blend.attachments[0].srcAlphaBlendFactor = BlendFactor::One;
        pd.blend.attachments[0].dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha;
        pd.debugName = "ScenePass.Transparent";
        m_transparentPipeline = device.CreatePipeline(pd);
    }
    return m_opaquePipeline.IsValid() && m_transparentPipeline.IsValid();
}

void ScenePass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!CreateShaders(device) || !CreatePipelines(device))
        return;

    BufferDesc bd{};
    bd.size = sizeof(SceneShadingUBO);
    bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
    bd.memory = MemoryUsage::CpuToGpu;
    bd.debugName = "ScenePass.UBO.Shading";
    m_shadingUbo = device.CreateBuffer(bd);
}

void ScenePass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_transparentPipeline.IsValid()) device.Destroy(m_transparentPipeline);
    if (m_opaquePipeline.IsValid()) device.Destroy(m_opaquePipeline);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    m_shadingUbo = {};
    m_transparentPipeline = {};
    m_opaquePipeline = {};
    m_fs = {};
    m_vs = {};
}

void ScenePass::Record(TitusRHI::IGDevice& device,
                       TitusRHI::RenderCommandList& cmd,
                       uint32_t /*frameIndex*/,
                       uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;
    if (m_ctx && m_ctx->mode != OITTechnique::Baseline)
        return;
    if (!m_scene || !m_opaquePipeline.IsValid() || !m_transparentPipeline.IsValid())
        return;

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("ScenePass::UpdateShading");
        SceneShadingUBO data{};
        FillSceneShadingUBO(data, m_ctx);
        device.UpdateBuffer(m_shadingUbo, &data, sizeof(data), 0);
    }

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
    rp.depthStencilOp.clearValue.stencil = 0;
    rp.renderArea.width = 0;
    rp.renderArea.height = 0;

    ZoneScopedN("ScenePass::Draw");
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

    ResourceSetDesc rs{};
    ResourceBindingValue ubo{};
    ubo.binding = 0;
    ubo.type = ResourceBindingType::UniformBuffer;
    ubo.buffer = m_shadingUbo;
    ubo.bufferOffset = 0;
    ubo.bufferRange = sizeof(SceneShadingUBO);
    rs.bindings.push_back(ubo);

    cmd.BindPipeline(m_opaquePipeline);
    cmd.BindResourceSet(0, rs);
    const auto& cornellAlbedo = m_scene->GetCornellAlbedo();
    DrawModelColored(cmd, m_scene->GetCornellHandle(), m_scene->GetCornellMatrix(),
                     cornellAlbedo.data(), cornellAlbedo.size(), 1.0f);

    cmd.BindPipeline(m_transparentPipeline);
    cmd.BindResourceSet(0, rs);
    const float dragonOpacity = m_ctx ? m_ctx->dragonOpacity : 0.40f;
    for (const auto& dragon : m_scene->GetDragons())
    {
        DrawModelColored(cmd, m_scene->GetDragonHandle(), dragon.modelMatrix,
                         &dragon.albedo, 1, dragonOpacity);
    }

    cmd.EndRenderPass();
}
