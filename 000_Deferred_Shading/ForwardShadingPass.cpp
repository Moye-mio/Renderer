// ============================================================================
// 000_Deferred_Shading - ForwardShadingPass.cpp
//
// 前向几何着色：Sponza -> 默认 backbuffer，片元循环 shared 点光。
// ============================================================================
#include "ForwardShadingPass.h"
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

ForwardShadingPass::ForwardShadingPass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void ForwardShadingPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    const std::string shaderDir = std::string(SOLUTION_DIR) + "000_Deferred_Shading/Shader/";
    const std::string vsPath = shaderDir + "Forward_VS.glsl";
    const std::string fsPath = shaderDir + "Forward_FS.glsl";
    std::vector<uint8_t> vsBytes, fsBytes;
    if (TitusAsset::ReadAllBytes(vsPath, vsBytes) && TitusAsset::ReadAllBytes(fsPath, fsBytes))
    {
        ShaderDesc vsDesc{};
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.code = vsBytes.data();
        vsDesc.bytes = vsBytes.size();
        vsDesc.entryPoint = "main";
        vsDesc.debugName = "ForwardShadingPass.VS";
        m_vs = device.CreateShader(vsDesc);

        ShaderDesc fsDesc{};
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.code = fsBytes.data();
        fsDesc.bytes = fsBytes.size();
        fsDesc.entryPoint = "main";
        fsDesc.debugName = "ForwardShadingPass.FS";
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
        {
            pd.vertexLayout = GetMeshSharedLayout(m_sponza->GetModelHandle());
        }

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

        ResourceBinding rbLight{};
        rbLight.name = "u_LightBlock";
        rbLight.set = 0;
        rbLight.binding = 2;
        rbLight.type = ResourceBindingType::UniformBuffer;
        rbLight.stages = ShaderStage::Fragment;
        pd.resourceBindings.push_back(rbLight);

        pd.debugName = "ForwardShadingPass.Pipeline";
        m_pipeline = device.CreatePipeline(pd);
    }
    else
    {
        LOG_STREAM_ERROR("ForwardShadingPass") << "shader files missing; pipeline not created";
    }

    {
        BufferDesc bd{};
        bd.size = sizeof(TitusMath::Mat4) * 2;
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "ForwardShading.UBO.Matrices";
        m_matricesUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(SharedShadingParams::LightBlockData);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "ForwardShading.UBO.Lights";
        m_lightUbo = device.CreateBuffer(bd);
    }
}

void ForwardShadingPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_lightUbo.IsValid()) device.Destroy(m_lightUbo);
    if (m_matricesUbo.IsValid()) device.Destroy(m_matricesUbo);
    if (m_pipeline.IsValid()) device.Destroy(m_pipeline);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    m_lightUbo = {};
    m_matricesUbo = {};
    m_pipeline = {};
    m_fs = {};
    m_vs = {};
}

void ForwardShadingPass::Record(TitusRHI::IGDevice& device,
                                TitusRHI::RenderCommandList& cmd,
                                uint32_t /*frameIndex*/,
                                uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_ctx || m_ctx->mode != ShadingTechnique::Forward)
        return;
    if (!m_pipeline.IsValid()) return;

    if (m_matricesUbo.IsValid())
    {
        ZoneScopedN("Forward::UpdateMatrices");
        TitusMath::Mat4 mats[2] = {
            CAMERA::GetMainCameraProjectionMatrix(),
            CAMERA::GetMainCameraViewMatrix()
        };
        device.UpdateBuffer(m_matricesUbo, mats, sizeof(mats), 0);
    }

    if (m_lightUbo.IsValid())
    {
        ZoneScopedN("Forward::UpdateLights");
        SharedShadingParams::LightBlockData data{};
        m_ctx->shared.FillLightBlock(data, CAMERA::GetMainCameraViewMatrix());
        device.UpdateBuffer(m_lightUbo, &data, sizeof(data), 0);
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
        ZoneScopedN("Forward::DrawModel");

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

            ResourceBindingValue lights{};
            lights.binding = 2;
            lights.type = ResourceBindingType::UniformBuffer;
            lights.buffer = m_lightUbo;
            lights.bufferOffset = 0;
            lights.bufferRange = sizeof(SharedShadingParams::LightBlockData);
            rs.bindings.push_back(lights);

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
