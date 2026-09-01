// ============================================================================
// 004_Anti_Aliasing - ScenePass.cpp
//
// 前向几何：Sponza -> 默认 backbuffer，Lambert + 漫反射。
// ============================================================================
#include "ScenePass.h"
#include "Sponza.h"
#include "TechniqueContext.h"

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
    TitusMath::Vec3 LightDirFromYawPitch(float yawDeg, float pitchDeg)
    {
        const float yaw = TitusMath::radians(yawDeg);
        const float pitch = TitusMath::radians(pitchDeg);
        const float cp = std::cos(pitch);
        return TitusMath::normalize(TitusMath::Vec3(
            cp * std::cos(yaw),
            std::sin(pitch),
            cp * std::sin(yaw)));
    }

    struct ShadingBlock
    {
        TitusMath::Vec4 lightDirVsAndAmbient{0.0f, 1.0f, 0.0f, 0.12f};
        TitusMath::Vec4 lightColor{1.0f, 0.96f, 0.88f, 0.0f};
    };
    static_assert(sizeof(ShadingBlock) == 32, "ShadingBlock std140 size");
}

ScenePass::ScenePass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void ScenePass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    const std::string shaderDir = std::string(SOLUTION_DIR) + "004_Anti_Aliasing/Shader/";
    const std::string vsPath = shaderDir + "Scene_VS.glsl";
    const std::string fsPath = shaderDir + "Scene_FS.glsl";
    std::vector<uint8_t> vsBytes, fsBytes;
    if (TitusAsset::ReadAllBytes(vsPath, vsBytes) && TitusAsset::ReadAllBytes(fsPath, fsBytes))
    {
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

        GraphicsPipelineDesc pd{};
        pd.vertexShader = m_vs;
        pd.fragmentShader = m_fs;
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

        ResourceBinding rbMats{};
        rbMats.name = "u_Matrices4ProjectionWorld";
        rbMats.set = 0;
        rbMats.binding = 0;
        rbMats.type = ResourceBindingType::UniformBuffer;
        rbMats.stages = ShaderStage::Vertex;
        pd.resourceBindings.push_back(rbMats);

        ResourceBinding rbDiff{};
        rbDiff.name = "u_DiffuseTexture";
        rbDiff.set = 0;
        rbDiff.binding = 1;
        rbDiff.type = ResourceBindingType::CombinedImageSampler;
        rbDiff.stages = ShaderStage::Fragment;
        pd.resourceBindings.push_back(rbDiff);

        ResourceBinding rbShading{};
        rbShading.name = "u_Shading";
        rbShading.set = 0;
        rbShading.binding = 2;
        rbShading.type = ResourceBindingType::UniformBuffer;
        rbShading.stages = ShaderStage::Fragment;
        pd.resourceBindings.push_back(rbShading);

        pd.debugName = "ScenePass.Pipeline";
        m_pipeline = device.CreatePipeline(pd);
    }
    else
    {
        LOG_STREAM_ERROR("ScenePass") << "shader files missing; pipeline not created";
    }

    {
        BufferDesc bd{};
        bd.size = sizeof(TitusMath::Mat4) * 2;
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "ScenePass.UBO.Matrices";
        m_matricesUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(ShadingBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "ScenePass.UBO.Shading";
        m_shadingUbo = device.CreateBuffer(bd);
    }
}

void ScenePass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_matricesUbo.IsValid()) device.Destroy(m_matricesUbo);
    if (m_pipeline.IsValid()) device.Destroy(m_pipeline);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    m_shadingUbo = {};
    m_matricesUbo = {};
    m_pipeline = {};
    m_fs = {};
    m_vs = {};
}

void ScenePass::Record(TitusRHI::IGDevice& device,
                       TitusRHI::RenderCommandList& cmd,
                       uint32_t /*frameIndex*/,
                       uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_ctx || m_ctx->mode != AATechnique::None)
        return;
    if (!m_pipeline.IsValid()) return;

    if (m_matricesUbo.IsValid())
    {
        ZoneScopedN("Scene::UpdateMatrices");
        TitusMath::Mat4 mats[2] = {
            CAMERA::GetMainCameraProjectionMatrix(),
            CAMERA::GetMainCameraViewMatrix()
        };
        device.UpdateBuffer(m_matricesUbo, mats, sizeof(mats), 0);
    }

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("Scene::UpdateShading");
        const float yaw = m_ctx ? m_ctx->lightYawDeg : 35.0f;
        const float pitch = m_ctx ? m_ctx->lightPitchDeg : 45.0f;
        const float ambient = m_ctx ? m_ctx->ambient : 0.12f;
        const TitusMath::Vec3 lightDirWs = LightDirFromYawPitch(yaw, pitch);
        const TitusMath::Vec4 lightDirVs =
            CAMERA::GetMainCameraViewMatrix() * TitusMath::Vec4(lightDirWs, 0.0f);

        ShadingBlock data{};
        data.lightDirVsAndAmbient = TitusMath::Vec4(TitusMath::Vec3(lightDirVs), ambient);
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

    {
        ZoneScopedN("Scene::DrawModel");

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

        {
            ResourceSetDesc rs{};

            ResourceBindingValue mats{};
            mats.binding = 0;
            mats.type = ResourceBindingType::UniformBuffer;
            mats.buffer = m_matricesUbo;
            mats.bufferOffset = 0;
            mats.bufferRange = sizeof(TitusMath::Mat4) * 2;
            rs.bindings.push_back(mats);

            ResourceBindingValue shading{};
            shading.binding = 2;
            shading.type = ResourceBindingType::UniformBuffer;
            shading.buffer = m_shadingUbo;
            shading.bufferOffset = 0;
            shading.bufferRange = sizeof(ShadingBlock);
            rs.bindings.push_back(shading);

            cmd.BindResourceSet(0, rs);
        }

        if (m_sponza)
        {
            const TitusMath::Mat4 model = m_sponza->GetModelMatrix();
            cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);
            DrawGpuModelWithDiffuse(cmd, m_sponza->GetModelHandle(), 0, 1);
        }

        cmd.EndRenderPass();
    }
}
