// ============================================================================
// 001_Reflective_shadow_map - RSMBufferPass.cpp
// 后端无关：派生自 TitusRHI::IRenderPass。
// 阶段 1（GL-only 路径①）：见 SponzaGBufferPass.cpp 顶部备注。
// ============================================================================
#include "RSMBufferPass.h"
#include "Sponza.h"

#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

RSMBufferPass::RSMBufferPass()
{
    passEvent = TitusRHI::ERenderPassEvent::AfterGBuffer;
}

void RSMBufferPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    // ------------------------------------------------------------------
    // 1) 光源 VP（与原实现一致）
    // ------------------------------------------------------------------
    constexpr TitusMath::Vec3 lightPos = TitusMath::Vec3(-0.15f, -1.13f, -0.58f);
    m_lightDir = TitusMath::normalize(TitusMath::Vec3(-1.0f, -0.7071f, 0.0f));
    const TitusMath::Mat4 lightView = TitusMath::lookAt(lightPos, lightPos + m_lightDir, TitusMath::Vec3(0, 1, 0));
    const TitusMath::Mat4 lightProj = TitusMath::ortho(-2.0f, 2.0f, -2.0f, 2.0f, 0.1f, 10.0f);
    m_lightVP = lightProj * lightView;

    // ------------------------------------------------------------------
    // 2) 创建 3 个色彩 + 1 个深度附件（统一 256x256）
    // ------------------------------------------------------------------
    auto makeColor = [&](const char* name)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R32G32B32A32_SFLOAT;
        td.width = static_cast<uint32_t>(m_resolution);
        td.height = static_cast<uint32_t>(m_resolution);
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = name;
        return device.CreateTexture(td);
    };
    m_fluxTex = makeColor("RSM.Flux");
    m_normalTex = makeColor("RSM.Normal");
    m_positionTex = makeColor("RSM.Position");

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::D32_SFLOAT;
        td.width = static_cast<uint32_t>(m_resolution);
        td.height = static_cast<uint32_t>(m_resolution);
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "RSM.Depth";
        m_depthTex = device.CreateTexture(td);
    }

    {
        RenderTargetDesc rt{};
        rt.width = static_cast<uint32_t>(m_resolution);
        rt.height = static_cast<uint32_t>(m_resolution);
        rt.colorAttachments.push_back({m_fluxTex, 0, 0});
        rt.colorAttachments.push_back({m_normalTex, 0, 0});
        rt.colorAttachments.push_back({m_positionTex, 0, 0});
        rt.depthStencilAttachment = {m_depthTex, 0, 0};
        rt.debugName = "RSM.RT";
        m_renderTarget = device.CreateRenderTarget(rt);
    }

    // ------------------------------------------------------------------
    // 3) Shader / Pipeline
    //    任务 8b：GL/VK 共用同一份 .glsl；VK 端 VKDevice::CreateShaderImpl 内部
    //    通过 glslang 在线编译为 SPIR-V。
    // ------------------------------------------------------------------
    const std::string shaderDir = std::string(SOLUTION_DIR) + "001_Reflective_shadow_map/Shader/";
    const std::string vsPath = shaderDir + "RSMBuffer_VS.glsl";
    const std::string fsPath = shaderDir + "RSMBuffer_FS.glsl";
    std::vector<uint8_t> vsBytes, fsBytes;
    if (TitusAsset::ReadAllBytes(vsPath, vsBytes) && TitusAsset::ReadAllBytes(fsPath, fsBytes))
    {
        ShaderDesc vsDesc{};
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.code = vsBytes.data();
        vsDesc.bytes = vsBytes.size();
        vsDesc.entryPoint = "main";
        vsDesc.debugName = "RSMBufferPass.VS";
        m_vs = device.CreateShader(vsDesc);

        ShaderDesc fsDesc{};
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.code = fsBytes.data();
        fsDesc.bytes = fsBytes.size();
        fsDesc.entryPoint = "main";
        fsDesc.debugName = "RSMBufferPass.FS";
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
        pd.blend.attachments.resize(3);
        pd.rtLayout.colorFormats = {
            Format::R32G32B32A32_SFLOAT,
            Format::R32G32B32A32_SFLOAT,
            Format::R32G32B32A32_SFLOAT
        };
        pd.rtLayout.depthStencilFormat = Format::D32_SFLOAT;

        // ⭐ 顶点布局：必须设置，否则 GL 后端 VAO 不启用 vertex attribute
        if (m_sponza)
        {
            pd.vertexLayout = TitusRHI::GetMeshSharedLayout(m_sponza->GetModelHandle());
        }

        // PushConstants: u_ModelMatrix + u_LightVPMatrix（以名字反向查 GLSL uniform）
        PushConstantRange pcModel{};
        pcModel.stages = ShaderStage::Vertex;
        pcModel.offset = 0;
        pcModel.size = sizeof(TitusMath::Mat4);
        pcModel.glName = "u_ModelMatrix";
        pd.pushConstantRanges.push_back(pcModel);
        PushConstantRange pcLightVP{};
        pcLightVP.stages = ShaderStage::Vertex;
        pcLightVP.offset = sizeof(TitusMath::Mat4);
        pcLightVP.size = sizeof(TitusMath::Mat4);
        pcLightVP.glName = "u_LightVPMatrix";
        pd.pushConstantRanges.push_back(pcLightVP);

        // 资源绑定：u_Matrices4ProjectionWorld UBO at set=0,binding=0
        ResourceBinding rbUbo{};
        rbUbo.name = "u_Matrices4ProjectionWorld";
        rbUbo.set = 0;
        rbUbo.binding = 0;
        rbUbo.type = ResourceBindingType::UniformBuffer;
        rbUbo.stages = ShaderStage::Vertex;
        pd.resourceBindings.push_back(rbUbo);
        // u_DiffuseTexture at set=0, binding=1
        // 任务 10：Vulkan 同 set 内 binding 唯一，UBO 占 0 后 sampler 使用 1。
        ResourceBinding rbDiff{};
        rbDiff.name = "u_DiffuseTexture";
        rbDiff.set = 0;
        rbDiff.binding = 1;
        rbDiff.type = ResourceBindingType::CombinedImageSampler;
        rbDiff.stages = ShaderStage::Fragment;
        pd.resourceBindings.push_back(rbDiff);

        pd.debugName = "RSMBufferPass.Pipeline";
        m_pipeline = device.CreatePipeline(pd);
    }
    else
    {
        LOG_STREAM_ERROR("RSMBufferPass") << "shader files missing; pipeline not created";
    }

    // ------------------------------------------------------------------
    // 4) UBO：u_Matrices4ProjectionWorld（mat4 proj + mat4 view）
    //    对 RSM 路径而言，view/proj 并不使用主相机的 view（只走 light VP）；
    //    但 GLSL 定义中仍含 u_ViewMatrix，顺手提供一份以保证 v2f_FragPosInViewSpace 同原版本。
    // ------------------------------------------------------------------
    {
        BufferDesc bd{};
        bd.size = sizeof(TitusMath::Mat4) * 2;
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "RSMBuffer.UBO.Matrices";
        m_matricesUbo = device.CreateBuffer(bd);
    }

    // ------------------------------------------------------------------
    // 4) 共享 RSM 输出 + 光源数据
    // ------------------------------------------------------------------
    using TitusRHI::TextureHandle;
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("RSMFluxTexture", m_fluxTex);
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("RSMNormalTexture", m_normalTex);
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("RSMPositionTexture", m_positionTex);
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TitusMath::Mat4>("LightVPMatrix", m_lightVP);
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<int>("RSMResolution", m_resolution);
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TitusMath::Vec3>("LightDir", m_lightDir);
}

void RSMBufferPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_matricesUbo.IsValid()) device.Destroy(m_matricesUbo);
    if (m_pipeline.IsValid()) device.Destroy(m_pipeline);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    if (m_renderTarget.IsValid()) device.Destroy(m_renderTarget);
    if (m_depthTex.IsValid()) device.Destroy(m_depthTex);
    if (m_positionTex.IsValid()) device.Destroy(m_positionTex);
    if (m_normalTex.IsValid()) device.Destroy(m_normalTex);
    if (m_fluxTex.IsValid()) device.Destroy(m_fluxTex);
    m_matricesUbo = {};
    m_pipeline = {};
    m_fs = {};
    m_vs = {};
    m_renderTarget = {};
    m_depthTex = {};
    m_positionTex = {};
    m_normalTex = {};
    m_fluxTex = {};
}

void RSMBufferPass::Record(TitusRHI::IGDevice& device,
                           TitusRHI::RenderCommandList& cmd,
                           uint32_t /*frameIndex*/,
                           uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    RenderPassBeginInfo rp{};
    rp.renderTarget = m_renderTarget;
    rp.renderArea.width = static_cast<uint32_t>(m_resolution);
    rp.renderArea.height = static_cast<uint32_t>(m_resolution);

    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.0f;
    colorOp.clearValue.color[1] = 0.0f;
    colorOp.clearValue.color[2] = 0.0f;
    colorOp.clearValue.color[3] = 1.0f;
    rp.colorOps.push_back(colorOp);
    rp.colorOps.push_back(colorOp);
    rp.colorOps.push_back(colorOp);

    rp.hasDepthStencil = true;
    rp.depthStencilOp.loadOp = LoadOp::Clear;
    rp.depthStencilOp.storeOp = StoreOp::Store;
    rp.depthStencilOp.clearValue.depth = 1.0f;

    cmd.BeginRenderPass(rp);

    Viewport vp{};
    vp.width = static_cast<float>(m_resolution);
    vp.height = static_cast<float>(m_resolution);
    cmd.SetViewport(vp);
    Rect2D sc{};
    sc.width = static_cast<uint32_t>(m_resolution);
    sc.height = static_cast<uint32_t>(m_resolution);
    cmd.SetScissor(sc);

    if (m_pipeline.IsValid()) cmd.BindPipeline(m_pipeline);

    // 更新 + 绑定 u_Matrices4ProjectionWorld UBO
    if (m_matricesUbo.IsValid())
    {
        TitusMath::Mat4 mats[2] = {
            TitusRHI::CAMERA::GetMainCameraProjectionMatrix(),
            TitusRHI::CAMERA::GetMainCameraViewMatrix()
        };
        device.UpdateBuffer(m_matricesUbo, mats, sizeof(mats), 0);

        ResourceSetDesc rs{};
        ResourceBindingValue bv{};
        bv.binding = 0;
        bv.type = ResourceBindingType::UniformBuffer;
        bv.buffer = m_matricesUbo;
        bv.bufferOffset = 0;
        bv.bufferRange = sizeof(mats);
        rs.bindings.push_back(bv);
        cmd.BindResourceSet(0, rs);
    }

    if (m_sponza)
    {
        const TitusMath::Mat4 model = m_sponza->GetModelMatrix();
        cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);
        cmd.PushConstants(ShaderStage::Vertex, sizeof(TitusMath::Mat4), sizeof(TitusMath::Mat4), &m_lightVP);

        TitusRHI::DrawGpuModelWithDiffuse(cmd, m_sponza->GetModelHandle(), 0, 1);
    }

    cmd.EndRenderPass();
}
