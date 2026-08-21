// ============================================================================
// 003_Toon_Shading - ToonPass.cpp
// ============================================================================
#include "ToonPass.h"
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

ToonPass::ToonPass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

bool ToonPass::CreateShaders(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    const std::string shaderDir = std::string(SOLUTION_DIR) + "003_Toon_Shading/Shader/";
    std::vector<uint8_t> vsBytes, fsBytes;
    if (!TitusAsset::ReadAllBytes(shaderDir + "Toon_VS.glsl", vsBytes) ||
        !TitusAsset::ReadAllBytes(shaderDir + "Toon_FS.glsl", fsBytes))
    {
        LOG_STREAM_ERROR("ToonPass") << "shader files missing; pipeline not created";
        return false;
    }

    ShaderDesc vsDesc{};
    vsDesc.stage = ShaderStage::Vertex;
    vsDesc.code = vsBytes.data();
    vsDesc.bytes = vsBytes.size();
    vsDesc.entryPoint = "main";
    vsDesc.debugName = "ToonPass.VS";
    m_vs = device.CreateShader(vsDesc);

    ShaderDesc fsDesc{};
    fsDesc.stage = ShaderStage::Fragment;
    fsDesc.code = fsBytes.data();
    fsDesc.bytes = fsBytes.size();
    fsDesc.entryPoint = "main";
    fsDesc.debugName = "ToonPass.FS";
    m_fs = device.CreateShader(fsDesc);
    return m_vs.IsValid() && m_fs.IsValid();
}

bool ToonPass::CreatePipeline(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!m_scene) return false;

    GraphicsPipelineDesc pd{};
    FillToonPipelineDesc(pd, m_vs, m_fs, m_scene->GetModelHandle());
    pd.debugName = "ToonPass.Diffuse";
    m_pipeline = device.CreatePipeline(pd);
    return m_pipeline.IsValid();
}

void ToonPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!CreateShaders(device) || !CreatePipeline(device))
        return;

    BufferDesc bd{};
    bd.size = sizeof(ToonShadingUBO);
    bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
    bd.memory = MemoryUsage::CpuToGpu;
    bd.debugName = "ToonPass.UBO.Shading";
    m_shadingUbo = device.CreateBuffer(bd);
}

void ToonPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_pipeline.IsValid()) device.Destroy(m_pipeline);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    m_shadingUbo = {};
    m_pipeline = {};
    m_fs = {};
    m_vs = {};
}

void ToonPass::Record(TitusRHI::IGDevice& device,
                      TitusRHI::RenderCommandList& cmd,
                      uint32_t /*frameIndex*/,
                      uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;
    if (!m_scene || !m_pipeline.IsValid())
        return;

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("ToonPass::UpdateShading");
        ToonShadingUBO data{};
        FillToonShadingUBO(data, m_ctx);
        device.UpdateBuffer(m_shadingUbo, &data, sizeof(data), 0);
    }

    RenderPassBeginInfo rp{};
    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.12f;
    colorOp.clearValue.color[1] = 0.13f;
    colorOp.clearValue.color[2] = 0.16f;
    colorOp.clearValue.color[3] = 1.0f;
    rp.colorOps.push_back(colorOp);
    rp.hasDepthStencil = true;
    rp.depthStencilOp.loadOp = LoadOp::Clear;
    rp.depthStencilOp.storeOp = StoreOp::DontCare;
    rp.depthStencilOp.clearValue.depth = 1.0f;
    rp.depthStencilOp.clearValue.stencil = 0;
    rp.renderArea.width = 0;
    rp.renderArea.height = 0;

    ZoneScopedN("ToonPass::Draw");
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

    cmd.BindPipeline(m_pipeline);

    ResourceSetDesc rs{};
    ResourceBindingValue ubo{};
    ubo.binding = 0;
    ubo.type = ResourceBindingType::UniformBuffer;
    ubo.buffer = m_shadingUbo;
    ubo.bufferOffset = 0;
    ubo.bufferRange = sizeof(ToonShadingUBO);
    rs.bindings.push_back(ubo);
    cmd.BindResourceSet(0, rs);

    const TitusMath::Mat4 model = m_scene->GetModelMatrix();
    cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);
    DrawGpuModelWithDiffuse(cmd, m_scene->GetModelHandle(), 0, 1);

    cmd.EndRenderPass();
}
