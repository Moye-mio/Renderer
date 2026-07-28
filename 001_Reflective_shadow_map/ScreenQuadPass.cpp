// ============================================================================
// 001_Reflective_shadow_map - ScreenQuadPass.cpp
//
// 后端无关：派生自 TitusRHI::IRenderPass。
// 从 RESOURCE_MANAGER 黑板取到 "ShadingTexture"，用全屏三角形采样到默认
// backbuffer。GLSL 沿用旧 ScreenQuad_VS / ScreenQuad_FS（顶点由 Shader
// 内硬编码 gl_VertexID 生成，不需要顶点输入）。
// ============================================================================
#include "ScreenQuadPass.h"

#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

ScreenQuadPass::ScreenQuadPass()
{
    passEvent = TitusRHI::ERenderPassEvent::FinalBlit;
}

void ScreenQuadPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    // 1) 从共享数据黑板取 ShadingTexture
    m_shadingTexture = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("ShadingTexture");
    if (!m_shadingTexture.IsValid())
    {
        LOG_STREAM_WARN("ScreenQuadPass") << "ShadingTexture not found in shared data; "
            "screen will sample undefined texture.";
    }

    // 2) Sampler（线性过滤 + clamp）
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Linear;
        sd.magFilter = FilterMode::Linear;
        sd.mipmapMode = MipmapMode::Linear;
        sd.addressU = AddressMode::ClampToEdge;
        sd.addressV = AddressMode::ClampToEdge;
        sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "ScreenQuadPass.Sampler";
        m_sampler = device.CreateSampler(sd);
    }

    // 3) Shader / Pipeline（任务 8b：GL/VK 共用 .glsl）
    const std::string shaderDir = std::string(SOLUTION_DIR) + "001_Reflective_shadow_map/Shader/";
    const std::string vsPath = shaderDir + "ScreenQuad_VS.glsl";
    const std::string fsPath = shaderDir + "ScreenQuad_FS.glsl";
    std::vector<uint8_t> vsBytes, fsBytes;
    if (TitusAsset::ReadAllBytes(vsPath, vsBytes) && TitusAsset::ReadAllBytes(fsPath, fsBytes))
    {
        ShaderDesc vsDesc{};
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.code = vsBytes.data();
        vsDesc.bytes = vsBytes.size();
        vsDesc.entryPoint = "main";
        vsDesc.debugName = "ScreenQuadPass.VS";
        m_vs = device.CreateShader(vsDesc);

        ShaderDesc fsDesc{};
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.code = fsBytes.data();
        fsDesc.bytes = fsBytes.size();
        fsDesc.entryPoint = "main";
        fsDesc.debugName = "ScreenQuadPass.FS";
        m_fs = device.CreateShader(fsDesc);

        GraphicsPipelineDesc pd{};
        pd.vertexShader = m_vs;
        pd.fragmentShader = m_fs;
        pd.topology = PrimitiveTopology::TriangleList;
        pd.rasterizer.cullMode = CullMode::None;
        pd.depthStencil.depthTestEnable = false;
        pd.depthStencil.depthWriteEnable = false;
        pd.blend.attachments.resize(1);
        // rtLayout.colorFormats 留空：VK 后端会自动使用 swapchain 默认 RenderPass（避免
        // B8G8R8A8_UNORM vs B8G8R8A8_SRGB 格式不匹配的 Validation 报错）。
        // GL 后端忽略 rtLayout，不受影响。

        // 资源绑定描述：set=0,binding=0 是采样纹理（u_Texture2D）
        ResourceBinding rb{};
        rb.name = "u_Texture2D";
        rb.set = 0;
        rb.binding = 0;
        rb.type = ResourceBindingType::CombinedImageSampler;
        rb.stages = ShaderStage::Fragment;
        pd.resourceBindings.push_back(rb);

        pd.debugName = "ScreenQuadPass.Pipeline";
        m_pipeline = device.CreatePipeline(pd);
    }
    else
    {
        LOG_STREAM_ERROR("ScreenQuadPass") << "shader files missing; pipeline not created";
    }
}

void ScreenQuadPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_pipeline.IsValid()) device.Destroy(m_pipeline);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    if (m_sampler.IsValid()) device.Destroy(m_sampler);
    m_pipeline = {};
    m_fs = {};
    m_vs = {};
    m_sampler = {};
}

void ScreenQuadPass::Record(TitusRHI::IGDevice& /*device*/,
                            TitusRHI::RenderCommandList& cmd,
                            uint32_t /*frameIndex*/,
                            uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    // 默认 backbuffer：renderTarget 留空
    RenderPassBeginInfo rp{};
    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.0f;
    colorOp.clearValue.color[1] = 0.0f;
    colorOp.clearValue.color[2] = 0.0f;
    colorOp.clearValue.color[3] = 1.0f;
    rp.colorOps.push_back(colorOp);
    // 默认 backbuffer RenderPass 含 color + depth 两个 attachment（VkSwapchainWrapper
    // 的 CreateDefaultRenderPass 里有 depth attachment），必须提供 depth clearValue，
    // 否则 Vulkan Validation 报 clearValueCount 不足。
    rp.hasDepthStencil = true;
    rp.depthStencilOp.loadOp = LoadOp::Clear;
    rp.depthStencilOp.storeOp = StoreOp::DontCare;
    rp.depthStencilOp.clearValue.depth = 1.0f;
    rp.depthStencilOp.clearValue.stencil = 0;
    // renderArea 留 0：VKCommandList::BeginRenderPass 会自动用 swapchain extent，
    // 避免 1920x1152 > framebuffer 1280x720 的 Validation 报错。
    rp.renderArea.width  = 0;
    rp.renderArea.height = 0;

    cmd.BeginRenderPass(rp);

    // viewport/scissor 使用 WINDOW_KEYWORD 的尺寸（VK 路径下已同步为业务配置的窗口大小）
    const uint32_t vpW = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowWidth());
    const uint32_t vpH = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowHeight());
    Viewport vp{};
    vp.width  = static_cast<float>(vpW);
    vp.height = static_cast<float>(vpH);
    cmd.SetViewport(vp);
    Rect2D sc{};
    sc.width  = vpW;
    sc.height = vpH;
    cmd.SetScissor(sc);

    if (m_pipeline.IsValid()) cmd.BindPipeline(m_pipeline);

    // 绑定采样纹理
    if (m_shadingTexture.IsValid() && m_sampler.IsValid())
    {
        ResourceSetDesc set{};
        ResourceBindingValue bv{};
        bv.binding = 0;
        bv.type = ResourceBindingType::CombinedImageSampler;
        bv.texture = m_shadingTexture;
        bv.sampler = m_sampler;
        set.bindings.push_back(bv);
        cmd.BindResourceSet(0, set);
    }

    cmd.Draw(3); // 全屏三角形：顶点由 ScreenQuad_VS 通过 gl_VertexID 生成

    cmd.EndRenderPass();
}
