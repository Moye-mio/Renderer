// ============================================================================
// 000_Deferred_Shading - SponzaGBufferPass.cpp
//
// 延迟渲染几何 Pass：
//   - 创建 3 张 RGBA32F 颜色 RT（Albedo / Normal / Position）+ 1 张 D32 深度；
//   - 通过 RegisterSharedData 把 4 张 RT 共享给 DeferredLightingPass；
//   - 使用 cmd.BeginRenderPass / BindPipeline / PushConstants / DrawGpuModel
//     录制命令，禁止任何原生 glXxx 调用。
// ============================================================================
#include "SponzaGBufferPass.h"
#include "Sponza.h"

#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"
#include "TracySupport.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

SponzaGBufferPass::SponzaGBufferPass()
{
    passEvent = TitusRHI::ERenderPassEvent::GBuffer;
}

void SponzaGBufferPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    m_width = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    m_height = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (m_width == 0) m_width = 1920;
    if (m_height == 0) m_height = 1152;

    // ------------------------------------------------------------------
    // 1) 创建 3 张颜色附件 + 1 张深度附件
    // ------------------------------------------------------------------
    auto makeColorRT = [&](const char* name)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R32G32B32A32_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = name;
        return device.CreateTexture(td);
    };
    m_albedoTex = makeColorRT("GBuffer.Albedo");
    m_normalTex = makeColorRT("GBuffer.Normal");
    m_positionTex = makeColorRT("GBuffer.Position");

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::D32_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::DepthStencilAttachment | TextureUsage::Sampled;
        td.debugName = "GBuffer.Depth";
        m_depthTex = device.CreateTexture(td);
    }

    // ------------------------------------------------------------------
    // 2) 组装 RenderTarget
    // ------------------------------------------------------------------
    {
        RenderTargetDesc rt{};
        rt.width = m_width;
        rt.height = m_height;
        rt.colorAttachments.push_back({m_albedoTex, 0, 0});
        rt.colorAttachments.push_back({m_normalTex, 0, 0});
        rt.colorAttachments.push_back({m_positionTex, 0, 0});
        rt.depthStencilAttachment = {m_depthTex, 0, 0};
        rt.debugName = "GBuffer.RT";
        m_renderTarget = device.CreateRenderTarget(rt);
    }

    // ------------------------------------------------------------------
    // 3) Shader / Pipeline
    // ------------------------------------------------------------------
    const std::string shaderDir = std::string(SOLUTION_DIR) + "000_Deferred_Shading/Shader/";
    const std::string vsPath = shaderDir + "Sponza_VS.glsl";
    const std::string fsPath = shaderDir + "Sponza_FS.glsl";
    std::vector<uint8_t> vsBytes, fsBytes;
    if (TitusAsset::ReadAllBytes(vsPath, vsBytes) && TitusAsset::ReadAllBytes(fsPath, fsBytes))
    {
        ShaderDesc vsDesc{};
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.code = vsBytes.data();
        vsDesc.bytes = vsBytes.size();
        vsDesc.entryPoint = "main";
        vsDesc.debugName = "SponzaGBufferPass.VS";
        m_vs = device.CreateShader(vsDesc);

        ShaderDesc fsDesc{};
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.code = fsBytes.data();
        fsDesc.bytes = fsBytes.size();
        fsDesc.entryPoint = "main";
        fsDesc.debugName = "SponzaGBufferPass.FS";
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

        // 顶点布局：从 GpuModel 取 sharedLayout，否则 GL 后端 VAO 缺少 attribute 配置。
        if (m_sponza)
        {
            pd.vertexLayout = GetMeshSharedLayout(m_sponza->GetModelHandle());
        }

        // 三个色彩附件均不启用混合
        pd.blend.attachments.resize(3);

        pd.rtLayout.colorFormats = {
            Format::R32G32B32A32_SFLOAT,
            Format::R32G32B32A32_SFLOAT,
            Format::R32G32B32A32_SFLOAT
        };
        pd.rtLayout.depthStencilFormat = Format::D32_SFLOAT;

        // Push constants：u_ModelMatrix (mat4)
        PushConstantRange pcModel{};
        pcModel.stages = ShaderStage::Vertex;
        pcModel.offset = 0;
        pcModel.size = sizeof(TitusMath::Mat4);
        pcModel.glName = "u_ModelMatrix";
        pd.pushConstantRanges.push_back(pcModel);

        // view/proj UBO at set=0,binding=0
        ResourceBinding rb{};
        rb.name = "u_Matrices4ProjectionWorld";
        rb.set = 0;
        rb.binding = 0;
        rb.type = ResourceBindingType::UniformBuffer;
        rb.stages = ShaderStage::Vertex;
        pd.resourceBindings.push_back(rb);

        // u_DiffuseTexture at set=0, binding=1
        ResourceBinding rbDiff{};
        rbDiff.name = "u_DiffuseTexture";
        rbDiff.set = 0;
        rbDiff.binding = 1;
        rbDiff.type = ResourceBindingType::CombinedImageSampler;
        rbDiff.stages = ShaderStage::Fragment;
        pd.resourceBindings.push_back(rbDiff);

        pd.debugName = "SponzaGBufferPass.Pipeline";
        m_pipeline = device.CreatePipeline(pd);
    }
    else
    {
        LOG_STREAM_ERROR("SponzaGBufferPass") << "shader files missing; pipeline not created";
    }

    // ------------------------------------------------------------------
    // 4) UBO：u_Matrices4ProjectionWorld（mat4 proj + mat4 view，std140）
    // ------------------------------------------------------------------
    {
        BufferDesc bd{};
        bd.size = sizeof(TitusMath::Mat4) * 2;
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "SponzaGBuffer.UBO.Matrices";
        m_matricesUbo = device.CreateBuffer(bd);
    }

    // ------------------------------------------------------------------
    // 5) 共享 G-Buffer 输出给 DeferredLightingPass
    // ------------------------------------------------------------------
    using TitusRHI::TextureHandle;
    RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("AlbedoTexture", m_albedoTex);
    RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("NormalTexture", m_normalTex);
    RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("PositionTexture", m_positionTex);
    RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("DepthTexture", m_depthTex);
}

void SponzaGBufferPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_matricesUbo.IsValid()) device.Destroy(m_matricesUbo);
    if (m_pipeline.IsValid()) device.Destroy(m_pipeline);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    if (m_renderTarget.IsValid()) device.Destroy(m_renderTarget);
    if (m_depthTex.IsValid()) device.Destroy(m_depthTex);
    if (m_positionTex.IsValid()) device.Destroy(m_positionTex);
    if (m_normalTex.IsValid()) device.Destroy(m_normalTex);
    if (m_albedoTex.IsValid()) device.Destroy(m_albedoTex);
    m_matricesUbo = {};
    m_pipeline = {};
    m_fs = {};
    m_vs = {};
    m_renderTarget = {};
    m_depthTex = {};
    m_positionTex = {};
    m_normalTex = {};
    m_albedoTex = {};
}

void SponzaGBufferPass::Record(TitusRHI::IGDevice& device,
                               TitusRHI::RenderCommandList& cmd,
                               uint32_t /*frameIndex*/,
                               uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    // BeginRenderPass：3 个颜色附件 Clear 为 0（背景干净），深度 Clear。
    RenderPassBeginInfo rp{};
    rp.renderTarget = m_renderTarget;
    rp.renderArea.width = m_width;
    rp.renderArea.height = m_height;

    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.0f;
    colorOp.clearValue.color[1] = 0.0f;
    colorOp.clearValue.color[2] = 0.0f;
    colorOp.clearValue.color[3] = 0.0f;
    rp.colorOps.push_back(colorOp);
    rp.colorOps.push_back(colorOp);
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

    if (m_pipeline.IsValid()) cmd.BindPipeline(m_pipeline);

    // 更新 + 绑定 u_Matrices4ProjectionWorld UBO（proj 在前，view 在后，与 GLSL std140 对齐）
    if (m_matricesUbo.IsValid())
    {
        ZoneScopedN("GBuffer::UpdateMatrices");
        TitusMath::Mat4 mats[2] = {
            CAMERA::GetMainCameraProjectionMatrix(),
            CAMERA::GetMainCameraViewMatrix()
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

    // PushConstants：仅发送 ModelMatrix；然后逐 SubMesh 绑定 diffuse 纹理并绘制。
    if (m_sponza)
    {
        ZoneScopedN("GBuffer::DrawModel");
        const TitusMath::Mat4 model = m_sponza->GetModelMatrix();
        cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);
        DrawGpuModelWithDiffuse(cmd, m_sponza->GetModelHandle(), 0, 1);
    }

    cmd.EndRenderPass();
}
