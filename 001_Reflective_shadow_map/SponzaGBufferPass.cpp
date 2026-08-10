// ============================================================================
// 001_Reflective_shadow_map - SponzaGBufferPass.cpp
//
// GL-only 版路径①：
//   - 派生自 TitusRHI::IRenderPass，所有资源通过 IGDevice 创建；
//   - GBuffer 4 个 RT 通过 RegisterSharedData 共享给后续 Pass；
//   - 使用 cmd.BeginRenderPass / BindPipeline / PushConstants / DrawGpuModel
//     录制命令，禁止任何原生 glXxx 调用。
//
// 备注（已知 GL PushConstants 限制）：
//   - GL 后端的 RenderCommandList::PushConstants 当前仅是 stub（未真正写入
//     uniform），见 Renderer/GLCommandList.cpp:285。这意味着即便本 Pass 的
//     形式 100% 合规，画面也可能空白；视觉等价需 GL 端 PushConstants 落地，
//     或 GLSL 改为 UBO + ResourceSet 直接绑。
//   - 路径①的硬约束验收点是 CI 静态扫描，本文件已满足。
// ============================================================================
#include "SponzaGBufferPass.h"
#include "Sponza.h"

#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"

// SOLUTION_DIR 由 vcxproj 通过 PreprocessorDefinitions 注入
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

    m_width = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowWidth());
    m_height = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowHeight());
    if (m_width == 0) m_width = 1920;
    if (m_height == 0) m_height = 1152;

    // ------------------------------------------------------------------
    // 1) 创建 4 个附件纹理
    // ------------------------------------------------------------------
    auto makeColorRT = [&](const char* name)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R32G32B32A32_SFLOAT; // 等价于旧 GL_RGBA32F
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
        td.format = Format::D32_SFLOAT; // 等价于旧 GL_DEPTH_COMPONENT32F
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
    // 3) Shader / Pipeline（GL/VK 共用 .glsl）
    // ------------------------------------------------------------------
    const std::string shaderDir = std::string(SOLUTION_DIR) + "001_Reflective_shadow_map/Shader/";
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
        // 临时关闭 backface culling：原项目沿用 GL 默认（无 cull），sponza.obj
        // 的三角形绕序未必与我们假设的 CCW 一致；先确保画面能渲出来，再视
        // 觉确认后回到 CullMode::Back / FrontFace::CCW（如确有需要）。
        pd.rasterizer.cullMode = CullMode::None;
        pd.rasterizer.frontFace = FrontFace::CounterClockwise;
        pd.depthStencil.depthTestEnable = true;
        pd.depthStencil.depthWriteEnable = true;
        pd.depthStencil.depthCompareOp = CompareOp::Less;

        // ⭐ 顶点布局：从 GpuModel 取 sharedLayout，否则 GL 后端 VAO 缺少
        // attribute 配置，glDrawElements 中所有 vertex attribute (location
        // 0/1/2/3/4) 都会读到全 0，模型变成在原点重合的退化几何（renderdoc
        // 中表现为 vertex 属性全 0）。
        if (m_sponza)
        {
            pd.vertexLayout = TitusRHI::GetMeshSharedLayout(m_sponza->GetModelHandle());
        }

        // 三个色彩附件均不启用混合
        pd.blend.attachments.resize(3);

        pd.rtLayout.colorFormats = {
            Format::R32G32B32A32_SFLOAT,
            Format::R32G32B32A32_SFLOAT,
            Format::R32G32B32A32_SFLOAT
        };
        pd.rtLayout.depthStencilFormat = Format::D32_SFLOAT;

        // Push constants：u_ModelMatrix (mat4)（与 Sponza_VS.glsl 的 `uniform mat4 u_ModelMatrix;` 对齐）
        PushConstantRange pcModel{};
        pcModel.stages = ShaderStage::Vertex;
        pcModel.offset = 0;
        pcModel.size = sizeof(TitusMath::Mat4);
        pcModel.glName = "u_ModelMatrix";
        pd.pushConstantRanges.push_back(pcModel);

        // 资源绑定：view/proj UBO at set=0,binding=0（与 Sponza_VS.glsl 的
        //   layout(std140, binding=0) uniform u_Matrices4ProjectionWorld { ... }
        // 对齐）
        ResourceBinding rb{};
        rb.name = "u_Matrices4ProjectionWorld";
        rb.set = 0;
        rb.binding = 0;
        rb.type = ResourceBindingType::UniformBuffer;
        rb.stages = ShaderStage::Vertex;
        pd.resourceBindings.push_back(rb);

        // u_DiffuseTexture at set=0, binding=1
        // Vulkan 同 set 内 binding 唯一，UBO 占 0 后 sampler 必须用 1。
        // 与 Sponza_FS.glsl 中 LAYOUT_BIND(0, 1) 严格对齐。
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
    // 4) 共享 GBuffer 输出给后续 Pass
    // ------------------------------------------------------------------
    using TitusRHI::TextureHandle;
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("AlbedoTexture", m_albedoTex);
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("NormalTexture", m_normalTex);
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("PositionTexture", m_positionTex);
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("DepthTexture", m_depthTex);
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

    // ------------------------------------------------------------------
    // BeginRenderPass：3 个颜色附件 Clear + 深度 Clear
    // ------------------------------------------------------------------
    RenderPassBeginInfo rp{};
    rp.renderTarget = m_renderTarget;
    rp.renderArea.width = m_width;
    rp.renderArea.height = m_height;

    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.2f;
    colorOp.clearValue.color[1] = 0.3f;
    colorOp.clearValue.color[2] = 0.4f;
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
    vp.width = static_cast<float>(m_width);
    vp.height = static_cast<float>(m_height);
    cmd.SetViewport(vp);
    Rect2D sc{};
    sc.width = m_width;
    sc.height = m_height;
    cmd.SetScissor(sc);

    if (m_pipeline.IsValid()) cmd.BindPipeline(m_pipeline);

    // 更新 + 绑定 u_Matrices4ProjectionWorld UBO（proj 放在前，view 放在后，与 GLSL std140 对齐）
    if (m_matricesUbo.IsValid())
    {
        TitusMath::Mat4 mats[2] = {
            TitusRHI::CAMERA::GetMainCameraProjectionMatrix(),
            TitusRHI::CAMERA::GetMainCameraViewMatrix()
        };
        // UpdateBuffer 不走录制队列，直接在 IGDevice 上调：GL 后端仅 glBufferSubData。
        // 这里我们不能调 device->——Record 中只拿到 RenderCommandList。
        // 改为在 Update 阶段更新：在 Record 里临时使用 device 引用。
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

    // PushConstants：仅发送 ModelMatrix（与 Sponza_VS.glsl 的 `uniform mat4 u_ModelMatrix;` 对齐）。
    // GL 后端会按 PushConstantRange.offset/size 反向查找 glName 并 glUniform。
    if (m_sponza)
    {
        const TitusMath::Mat4 model = m_sponza->GetModelMatrix();
        cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);

        // 绘制 Sponza 模型，每个 SubMesh 绑定其 Diffuse 纹理到 set=0, binding=1
        TitusRHI::DrawGpuModelWithDiffuse(cmd, m_sponza->GetModelHandle(), 0, 1);
    }

    cmd.EndRenderPass();
}
