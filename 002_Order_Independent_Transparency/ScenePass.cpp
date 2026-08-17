// ============================================================================
// 002_Order_Independent_Transparency - ScenePass.cpp
//
// 不透明 Cornell → 朴素 Alpha 半透明 Dragon，画到默认 backbuffer。
// ============================================================================
#include "ScenePass.h"
#include "Scene.h"
#include "TechniqueContext.h"

#include <cstddef>
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
    struct SceneShadingUBO
    {
        TitusMath::Mat4 projection{1.0f};
        TitusMath::Mat4 view{1.0f};
        TitusMath::Vec4 lightDirVSAndAmbient{0.0f, 1.0f, 0.0f, 0.22f};
        TitusMath::Vec4 lightColor{1.0f, 0.96f, 0.88f, 0.0f};
    };
    static_assert(sizeof(SceneShadingUBO) == 160, "SceneShadingUBO std140 size");

    void FillPipelineShared(TitusRHI::GraphicsPipelineDesc& pd,
                            TitusRHI::ShaderHandle vs,
                            TitusRHI::ShaderHandle fs,
                            TitusRHI::GpuModelHandle layoutSource)
    {
        using namespace TitusRHI;
        pd.vertexShader = vs;
        pd.fragmentShader = fs;
        pd.topology = PrimitiveTopology::TriangleList;
        pd.rasterizer.cullMode = CullMode::None;
        pd.rasterizer.frontFace = FrontFace::CounterClockwise;
        pd.depthStencil.depthTestEnable = true;
        pd.depthStencil.depthCompareOp = CompareOp::Less;
        pd.blend.attachments.resize(1);
        if (layoutSource.IsValid())
            pd.vertexLayout = GetMeshSharedLayout(layoutSource);

        PushConstantRange pcModel{};
        pcModel.stages = ShaderStage::Vertex;
        pcModel.offset = 0;
        pcModel.size = sizeof(TitusMath::Mat4);
        pcModel.glName = "u_ModelMatrix";
        pd.pushConstantRanges.push_back(pcModel);

        PushConstantRange pcAlbedo{};
        pcAlbedo.stages = ShaderStage::Fragment;
        pcAlbedo.offset = sizeof(TitusMath::Mat4);
        pcAlbedo.size = sizeof(TitusMath::Vec4);
        pcAlbedo.glName = "u_AlbedoOpacity";
        pd.pushConstantRanges.push_back(pcAlbedo);

        ResourceBinding rb{};
        rb.name = "u_SceneShading";
        rb.set = 0;
        rb.binding = 0;
        rb.type = ResourceBindingType::UniformBuffer;
        rb.stages = ShaderStage::Vertex | ShaderStage::Fragment;
        pd.resourceBindings.push_back(rb);
    }

    void DrawModelColored(TitusRHI::RenderCommandList& cmd,
                          TitusRHI::GpuModelHandle handle,
                          const TitusMath::Mat4& model,
                          const TitusMath::Vec3* albedos,
                          size_t albedoCount,
                          float opacity)
    {
        using namespace TitusRHI;
        const void* p = APP::GetGpuModelInternal(handle);
        if (!p) return;
        const GpuModel* gpu = static_cast<const GpuModel*>(p);
        const auto& mesh = gpu->GetMesh();

        cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);
        for (size_t i = 0; i < mesh.subMeshes.size(); ++i)
        {
            const TitusMath::Vec3 albedo = (!albedos || albedoCount == 0)
                ? TitusMath::Vec3(1.0f)
                : albedos[i < albedoCount ? i : albedoCount - 1];
            const TitusMath::Vec4 albedoOpacity(albedo, opacity);
            cmd.PushConstants(ShaderStage::Fragment, sizeof(TitusMath::Mat4),
                              sizeof(TitusMath::Vec4), &albedoOpacity);

            const auto& sub = mesh.subMeshes[i];
            if (sub.vertexBuffer.IsValid())
                cmd.BindVertexBuffer(0, sub.vertexBuffer, 0);
            if (sub.indexCount > 0 && sub.indexBuffer.IsValid())
            {
                cmd.BindIndexBuffer(sub.indexBuffer, sub.indexType, 0);
                cmd.DrawIndexed(sub.indexCount, 1, 0, 0, 0);
            }
            else
            {
                cmd.Draw(sub.vertexCount, 1, 0, 0);
            }
        }
    }
}

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
        FillPipelineShared(pd, m_vs, m_fs, m_scene->GetCornellHandle());
        pd.depthStencil.depthWriteEnable = true;
        pd.debugName = "ScenePass.Opaque";
        m_opaquePipeline = device.CreatePipeline(pd);
    }
    {
        GraphicsPipelineDesc pd{};
        FillPipelineShared(pd, m_vs, m_fs, m_scene->GetDragonHandle());
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
    if (!m_scene || !m_opaquePipeline.IsValid() || !m_transparentPipeline.IsValid())
        return;

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("ScenePass::UpdateShading");
        SceneShadingUBO data{};
        data.projection = CAMERA::GetMainCameraProjectionMatrix();
        data.view = CAMERA::GetMainCameraViewMatrix();
        const TitusMath::Vec3 lightDirWS = TitusMath::normalize(TitusMath::Vec3(0.18f, 1.0f, 0.35f));
        const TitusMath::Vec4 lightDirVS = data.view * TitusMath::Vec4(lightDirWS, 0.0f);
        data.lightDirVSAndAmbient = TitusMath::Vec4(TitusMath::Vec3(lightDirVS), 0.22f);
        data.lightColor = TitusMath::Vec4(1.0f, 0.96f, 0.88f, 0.0f);
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
