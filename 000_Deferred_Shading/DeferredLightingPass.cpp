// ============================================================================
// 000_Deferred_Shading - DeferredLightingPass.cpp
//
// 全屏光照 Pass：读取 G-Buffer -> 对 5 个点光源做视空间 Blinn-Phong -> 输出到
// 默认 backbuffer。所有资源经 IGDevice 创建，命令经 RenderCommandList 录制。
// ============================================================================
#include "DeferredLightingPass.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

DeferredLightingPass::DeferredLightingPass()
{
    passEvent = TitusRHI::ERenderPassEvent::Lighting;
}

void DeferredLightingPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    // 1) 采样 G-Buffer 用的 sampler（Nearest + Clamp，配合 texelFetch 1:1 采样）
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "DeferredLightingPass.Sampler";
        m_sampler = device.CreateSampler(sd);
    }

    // 2) 光源 UBO（binding=0），大小与 std140 布局对齐
    {
        BufferDesc bd{};
        bd.size = sizeof(LightBlockData);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "DeferredLightingPass.LightUBO";
        m_lightUbo = device.CreateBuffer(bd);
    }

    // 3) Shader / Pipeline（全屏三角形，无顶点输入，输出到 backbuffer）
    const std::string shaderDir = std::string(SOLUTION_DIR) + "000_Deferred_Shading/Shader/";
    const std::string vsPath = shaderDir + "DeferredLighting_VS.glsl";
    const std::string fsPath = shaderDir + "DeferredLighting_FS.glsl";
    std::vector<uint8_t> vsBytes, fsBytes;
    if (TitusAsset::ReadAllBytes(vsPath, vsBytes) && TitusAsset::ReadAllBytes(fsPath, fsBytes))
    {
        ShaderDesc vsDesc{};
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.code = vsBytes.data();
        vsDesc.bytes = vsBytes.size();
        vsDesc.entryPoint = "main";
        vsDesc.debugName = "DeferredLightingPass.VS";
        m_vs = device.CreateShader(vsDesc);

        ShaderDesc fsDesc{};
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.code = fsBytes.data();
        fsDesc.bytes = fsBytes.size();
        fsDesc.entryPoint = "main";
        fsDesc.debugName = "DeferredLightingPass.FS";
        m_fs = device.CreateShader(fsDesc);

        GraphicsPipelineDesc pd{};
        pd.vertexShader = m_vs;
        pd.fragmentShader = m_fs;
        pd.topology = PrimitiveTopology::TriangleList;
        pd.rasterizer.cullMode = CullMode::None;
        pd.depthStencil.depthTestEnable = false;
        pd.depthStencil.depthWriteEnable = false;
        pd.blend.attachments.resize(1);
        // rtLayout.colorFormats 留空：VK 后端会自动使用 swapchain 默认 RenderPass；
        // GL 后端忽略 rtLayout。

        // 资源绑定：binding=0 光源 UBO；binding=1..3 三张 G-Buffer 纹理。
        ResourceBinding ubo{};
        ubo.name = "u_LightBlock";
        ubo.set = 0;
        ubo.binding = 0;
        ubo.type = ResourceBindingType::UniformBuffer;
        ubo.stages = ShaderStage::Fragment;
        pd.resourceBindings.push_back(ubo);

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
        addSampler("u_AlbedoTexture", 1);
        addSampler("u_NormalTexture", 2);
        addSampler("u_PositionTexture", 3);

        pd.debugName = "DeferredLightingPass.Pipeline";
        m_pipeline = device.CreatePipeline(pd);
    }
    else
    {
        LOG_STREAM_ERROR("DeferredLightingPass") << "shader files missing; pipeline not created";
    }
}

void DeferredLightingPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_pipeline.IsValid()) device.Destroy(m_pipeline);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    if (m_lightUbo.IsValid()) device.Destroy(m_lightUbo);
    if (m_sampler.IsValid()) device.Destroy(m_sampler);
    m_pipeline = {};
    m_fs = {};
    m_vs = {};
    m_lightUbo = {};
    m_sampler = {};
}

void DeferredLightingPass::Record(TitusRHI::IGDevice& device,
                                  TitusRHI::RenderCommandList& cmd,
                                  uint32_t /*frameIndex*/,
                                  uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_pipeline.IsValid()) return;

    // 取共享 G-Buffer 纹理（GBufferPass 在其 Init 中已注册）
    using TitusRHI::TextureHandle;
    TextureHandle albedo = RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("AlbedoTexture");
    TextureHandle normal = RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("NormalTexture");
    TextureHandle position = RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("PositionTexture");

    // 每帧把世界空间光源变换到视空间，写入 UBO
    if (m_lightUbo.IsValid())
    {
        LightBlockData data{};
        const TitusMath::Mat4 view = CAMERA::GetMainCameraViewMatrix();
        const int n = static_cast<int>(std::min(m_lights.size(), static_cast<size_t>(MAX_LIGHTS)));
        for (int i = 0; i < n; ++i)
        {
            const TitusMath::Vec4 posVS = view * TitusMath::Vec4(m_lights[i].worldPos, 1.0f);
            data.lights[i].positionVSAndRadius = TitusMath::Vec4(TitusMath::Vec3(posVS), m_lights[i].radius);
            data.lights[i].colorAndIntensity = TitusMath::Vec4(m_lights[i].color, m_lights[i].intensity);
        }
        data.count = TitusMath::IVec4(n, 0, 0, 0);
        device.UpdateBuffer(m_lightUbo, &data, sizeof(data), 0);
    }

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
    rp.hasDepthStencil = true;
    rp.depthStencilOp.loadOp = LoadOp::Clear;
    rp.depthStencilOp.storeOp = StoreOp::DontCare;
    rp.depthStencilOp.clearValue.depth = 1.0f;
    rp.depthStencilOp.clearValue.stencil = 0;
    rp.renderArea.width = 0;
    rp.renderArea.height = 0;

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

    // 绑定 UBO + 3 张 G-Buffer 纹理
    {
        ResourceSetDesc rs{};

        ResourceBindingValue ubo{};
        ubo.binding = 0;
        ubo.type = ResourceBindingType::UniformBuffer;
        ubo.buffer = m_lightUbo;
        ubo.bufferOffset = 0;
        ubo.bufferRange = sizeof(LightBlockData);
        rs.bindings.push_back(ubo);

        auto pushTex = [&](TextureHandle h, uint32_t binding)
        {
            ResourceBindingValue bv{};
            bv.binding = binding;
            bv.type = ResourceBindingType::CombinedImageSampler;
            bv.texture = h;
            bv.sampler = m_sampler;
            rs.bindings.push_back(bv);
        };
        pushTex(albedo, 1);
        pushTex(normal, 2);
        pushTex(position, 3);

        cmd.BindResourceSet(0, rs);
    }

    cmd.Draw(3); // 全屏三角形

    cmd.EndRenderPass();
}
