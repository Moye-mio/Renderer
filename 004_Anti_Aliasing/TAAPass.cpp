// ============================================================================
// 004_Anti_Aliasing - TAAPass.cpp
//
// jitter 前向 MRT → 时域 resolve 写 history → 全屏拷回 backbuffer。
// ============================================================================
#include "TAAPass.h"
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
    constexpr TitusRHI::Format kColorFormat = TitusRHI::Format::R16G16B16A16_SFLOAT;
    constexpr TitusRHI::Format kVelocityFormat = TitusRHI::Format::R16G16_SFLOAT;
    constexpr TitusRHI::Format kDepthFormat = TitusRHI::Format::D32_SFLOAT;
    constexpr uint32_t kHaltonTaps = 16;

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

    struct TAASceneBlock
    {
        TitusMath::Mat4 projection{1.0f};
        TitusMath::Mat4 view{1.0f};
        TitusMath::Mat4 prevViewProj{1.0f};
        TitusMath::Vec4 jitter{0.0f, 0.0f, 0.0f, 0.0f};
    };
    static_assert(sizeof(TAASceneBlock) == 208, "TAASceneBlock std140 size");

    struct TAAResolveBlock
    {
        TitusMath::Vec4 params{0.1f, 1.0f, 0.0f, 0.0f};
        TitusMath::Vec4 jitter{0.0f, 0.0f, 0.0f, 0.0f};
    };
    static_assert(sizeof(TAAResolveBlock) == 32, "TAAResolveBlock std140 size");

    float Halton(uint32_t index, uint32_t base)
    {
        float f = 1.0f;
        float r = 0.0f;
        while (index > 0)
        {
            f /= static_cast<float>(base);
            r += f * static_cast<float>(index % base);
            index /= base;
        }
        return r;
    }

    // Halton(2,3) 映射到 NDC：一像素 = 2/width。index 从 1 起跳过 (0,0)。
    TitusMath::Vec2 HaltonJitterNdc(uint32_t index, uint32_t width, uint32_t height, float scale)
    {
        const uint32_t sample = (index % kHaltonTaps) + 1;
        const float hx = Halton(sample, 2);
        const float hy = Halton(sample, 3);
        const float w = width > 0 ? static_cast<float>(width) : 1.0f;
        const float h = height > 0 ? static_cast<float>(height) : 1.0f;
        return {
            (hx - 0.5f) * 2.0f / w * scale,
            (hy - 0.5f) * 2.0f / h * scale
        };
    }

    bool LoadShaderBytes(const std::string& path, std::vector<uint8_t>& out)
    {
        return TitusAsset::ReadAllBytes(path, out);
    }

}

TAAPass::TAAPass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void TAAPass::ResetHistory()
{
    m_historyValid = false;
    m_hasPrevViewProj = false;
    m_jitterIndex = 0;
    m_historyWrite = 0;
}

void TAAPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    const std::string shaderDir = std::string(SOLUTION_DIR) + "004_Anti_Aliasing/Shader/";

    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "TAA_Scene_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "TAA_Scene_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "TAAPass.SceneVS";
            m_sceneVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "TAAPass.SceneFS";
            m_sceneFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_sceneVS;
            pd.fragmentShader = m_sceneFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.rasterizer.frontFace = FrontFace::CounterClockwise;
            pd.depthStencil.depthTestEnable = true;
            pd.depthStencil.depthWriteEnable = true;
            pd.depthStencil.depthCompareOp = CompareOp::Less;
            pd.blend.attachments.resize(2);
            pd.rtLayout.colorFormats = {kColorFormat, kVelocityFormat};
            pd.rtLayout.depthStencilFormat = kDepthFormat;

            if (m_sponza)
                pd.vertexLayout = GetMeshSharedLayout(m_sponza->GetModelHandle());

            PushConstantRange pcModel{};
            pcModel.stages = ShaderStage::Vertex;
            pcModel.offset = 0;
            pcModel.size = sizeof(TitusMath::Mat4);
            pcModel.glName = "u_ModelMatrix";
            pd.pushConstantRanges.push_back(pcModel);

            ResourceBinding rbScene{};
            rbScene.name = "u_TAAScene";
            rbScene.set = 0;
            rbScene.binding = 0;
            rbScene.type = ResourceBindingType::UniformBuffer;
            rbScene.stages = ShaderStage::Vertex;
            pd.resourceBindings.push_back(rbScene);

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

            pd.debugName = "TAAPass.ScenePipeline";
            m_scenePipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("TAAPass") << "scene shaders missing";
        }
    }

    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "Blit_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "TAA_Resolve_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "TAAPass.ResolveVS";
            m_resolveVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "TAAPass.ResolveFS";
            m_resolveFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_resolveVS;
            pd.fragmentShader = m_resolveFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.depthStencil.depthTestEnable = false;
            pd.depthStencil.depthWriteEnable = false;
            pd.blend.attachments.resize(1);
            pd.rtLayout.colorFormats = {kColorFormat};

            ResourceBinding rbCurr{};
            rbCurr.name = "u_CurrColor";
            rbCurr.set = 0;
            rbCurr.binding = 0;
            rbCurr.type = ResourceBindingType::CombinedImageSampler;
            rbCurr.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbCurr);

            ResourceBinding rbVel{};
            rbVel.name = "u_Velocity";
            rbVel.set = 0;
            rbVel.binding = 1;
            rbVel.type = ResourceBindingType::CombinedImageSampler;
            rbVel.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbVel);

            ResourceBinding rbHist{};
            rbHist.name = "u_History";
            rbHist.set = 0;
            rbHist.binding = 2;
            rbHist.type = ResourceBindingType::CombinedImageSampler;
            rbHist.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbHist);

            ResourceBinding rbParams{};
            rbParams.name = "u_TAAParams";
            rbParams.set = 0;
            rbParams.binding = 3;
            rbParams.type = ResourceBindingType::UniformBuffer;
            rbParams.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbParams);

            pd.debugName = "TAAPass.ResolvePipeline";
            m_resolvePipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("TAAPass") << "resolve shaders missing";
        }
    }

    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "Blit_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "Blit_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "TAAPass.BlitVS";
            m_blitVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "TAAPass.BlitFS";
            m_blitFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_blitVS;
            pd.fragmentShader = m_blitFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.depthStencil.depthTestEnable = false;
            pd.depthStencil.depthWriteEnable = false;
            pd.blend.attachments.resize(1);

            ResourceBinding rb{};
            rb.name = "u_ShadeColor";
            rb.set = 0;
            rb.binding = 0;
            rb.type = ResourceBindingType::CombinedImageSampler;
            rb.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rb);
            pd.debugName = "TAAPass.BlitPipeline";
            m_blitPipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("TAAPass") << "blit shaders missing";
        }
    }

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "TAAPass.PointSampler";
        m_pointSampler = device.CreateSampler(sd);
    }
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Linear;
        sd.magFilter = FilterMode::Linear;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "TAAPass.LinearSampler";
        m_linearSampler = device.CreateSampler(sd);
    }

    {
        BufferDesc bd{};
        bd.size = sizeof(TAASceneBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "TAAPass.UBO.Scene";
        m_sceneUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(ShadingBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "TAAPass.UBO.Shading";
        m_shadingUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(TAAResolveBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "TAAPass.UBO.Resolve";
        m_resolveUbo = device.CreateBuffer(bd);
    }

    uint32_t w = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    uint32_t h = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (w == 0) w = 1920;
    if (h == 0) h = 1152;
    EnsureTargets(device, w, h);
}

void TAAPass::DestroyTargets(TitusRHI::IGDevice& device)
{
    if (m_sceneRT.IsValid()) device.Destroy(m_sceneRT);
    if (m_historyRT[0].IsValid()) device.Destroy(m_historyRT[0]);
    if (m_historyRT[1].IsValid()) device.Destroy(m_historyRT[1]);
    if (m_history[0].IsValid()) device.Destroy(m_history[0]);
    if (m_history[1].IsValid()) device.Destroy(m_history[1]);
    if (m_depth.IsValid()) device.Destroy(m_depth);
    if (m_velocity.IsValid()) device.Destroy(m_velocity);
    if (m_currColor.IsValid()) device.Destroy(m_currColor);
    m_sceneRT = {};
    m_historyRT[0] = {};
    m_historyRT[1] = {};
    m_history[0] = {};
    m_history[1] = {};
    m_depth = {};
    m_velocity = {};
    m_currColor = {};
}

void TAAPass::EnsureTargets(TitusRHI::IGDevice& device, uint32_t width, uint32_t height)
{
    using namespace TitusRHI;

    if (width == 0 || height == 0)
        return;
    if (m_width == width && m_height == height && m_sceneRT.IsValid()
        && m_historyRT[0].IsValid() && m_historyRT[1].IsValid())
        return;

    device.WaitIdle();
    DestroyTargets(device);

    auto makeColor = [&](Format fmt, TextureUsage usage, const char* name)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = fmt;
        td.width = width;
        td.height = height;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = usage;
        td.debugName = name;
        return device.CreateTexture(td);
    };

    m_currColor = makeColor(
        kColorFormat,
        TextureUsage::ColorAttachment | TextureUsage::Sampled,
        "TAAPass.CurrColor");
    m_velocity = makeColor(
        kVelocityFormat,
        TextureUsage::ColorAttachment | TextureUsage::Sampled,
        "TAAPass.Velocity");
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kDepthFormat;
        td.width = width;
        td.height = height;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "TAAPass.Depth";
        m_depth = device.CreateTexture(td);
    }
    m_history[0] = makeColor(
        kColorFormat,
        TextureUsage::ColorAttachment | TextureUsage::Sampled,
        "TAAPass.History0");
    m_history[1] = makeColor(
        kColorFormat,
        TextureUsage::ColorAttachment | TextureUsage::Sampled,
        "TAAPass.History1");

    {
        RenderTargetDesc rt{};
        rt.width = width;
        rt.height = height;
        rt.colorAttachments.push_back({m_currColor, 0, 0});
        rt.colorAttachments.push_back({m_velocity, 0, 0});
        rt.depthStencilAttachment = {m_depth, 0, 0};
        rt.debugName = "TAAPass.SceneRT";
        m_sceneRT = device.CreateRenderTarget(rt);
    }
    for (int i = 0; i < 2; ++i)
    {
        RenderTargetDesc rt{};
        rt.width = width;
        rt.height = height;
        rt.colorAttachments.push_back({m_history[i], 0, 0});
        rt.debugName = (i == 0) ? "TAAPass.HistoryRT0" : "TAAPass.HistoryRT1";
        m_historyRT[i] = device.CreateRenderTarget(rt);
    }

    m_width = width;
    m_height = height;
    ResetHistory();

    LOG_STREAM_INFO("TAAPass") << "TAA targets " << width << "x" << height;
}

void TAAPass::Destroy(TitusRHI::IGDevice& device)
{
    DestroyTargets(device);
    if (m_resolveUbo.IsValid()) device.Destroy(m_resolveUbo);
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_sceneUbo.IsValid()) device.Destroy(m_sceneUbo);
    if (m_linearSampler.IsValid()) device.Destroy(m_linearSampler);
    if (m_pointSampler.IsValid()) device.Destroy(m_pointSampler);
    if (m_blitPipeline.IsValid()) device.Destroy(m_blitPipeline);
    if (m_blitFS.IsValid()) device.Destroy(m_blitFS);
    if (m_blitVS.IsValid()) device.Destroy(m_blitVS);
    if (m_resolvePipeline.IsValid()) device.Destroy(m_resolvePipeline);
    if (m_resolveFS.IsValid()) device.Destroy(m_resolveFS);
    if (m_resolveVS.IsValid()) device.Destroy(m_resolveVS);
    if (m_scenePipeline.IsValid()) device.Destroy(m_scenePipeline);
    if (m_sceneFS.IsValid()) device.Destroy(m_sceneFS);
    if (m_sceneVS.IsValid()) device.Destroy(m_sceneVS);
    m_resolveUbo = {};
    m_shadingUbo = {};
    m_sceneUbo = {};
    m_linearSampler = {};
    m_pointSampler = {};
    m_blitPipeline = {};
    m_blitFS = {};
    m_blitVS = {};
    m_resolvePipeline = {};
    m_resolveFS = {};
    m_resolveVS = {};
    m_scenePipeline = {};
    m_sceneFS = {};
    m_sceneVS = {};
    m_width = 0;
    m_height = 0;
    ResetHistory();
}

void TAAPass::Record(TitusRHI::IGDevice& device,
                     TitusRHI::RenderCommandList& cmd,
                     uint32_t /*frameIndex*/,
                     uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_ctx || m_ctx->mode != AATechnique::TAA)
        return;
    if (!m_scenePipeline.IsValid() || !m_resolvePipeline.IsValid() || !m_blitPipeline.IsValid())
        return;

    const uint32_t vpW = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    const uint32_t vpH = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    EnsureTargets(device, vpW, vpH);

    if (!m_sceneRT.IsValid() || !m_historyRT[0].IsValid() || !m_historyRT[1].IsValid())
        return;

    if (m_ctx->taaResetHistory)
    {
        ResetHistory();
        m_ctx->taaResetHistory = false;
    }

    const TitusMath::Mat4 proj = CAMERA::GetMainCameraProjectionMatrix();
    const TitusMath::Mat4 view = CAMERA::GetMainCameraViewMatrix();
    const TitusMath::Mat4 currViewProj = proj * view;
    const float jitterScale = m_ctx ? m_ctx->taaJitterScale : 1.0f;
    const TitusMath::Vec2 jitter = HaltonJitterNdc(m_jitterIndex, m_width, m_height, jitterScale);

    if (m_sceneUbo.IsValid())
    {
        ZoneScopedN("TAA::UpdateScene");
        TAASceneBlock data{};
        data.projection = proj;
        data.view = view;
        data.prevViewProj = m_hasPrevViewProj ? m_prevViewProj : currViewProj;
        data.jitter = TitusMath::Vec4(jitter.x, jitter.y, 0.0f, 0.0f);
        device.UpdateBuffer(m_sceneUbo, &data, sizeof(data), 0);
    }

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("TAA::UpdateShading");
        const TitusMath::Vec3 lightDirWs =
            LightDirFromYawPitch(m_ctx->lightYawDeg, m_ctx->lightPitchDeg);
        const TitusMath::Vec4 lightDirVs =
            CAMERA::GetMainCameraViewMatrix() * TitusMath::Vec4(lightDirWs, 0.0f);

        ShadingBlock data{};
        data.lightDirVsAndAmbient = TitusMath::Vec4(TitusMath::Vec3(lightDirVs), m_ctx->ambient);
        data.lightColor = TitusMath::Vec4(1.0f, 0.96f, 0.88f, 0.0f);
        device.UpdateBuffer(m_shadingUbo, &data, sizeof(data), 0);
    }

    {
        ZoneScopedN("TAA::DrawModel");

        RenderPassBeginInfo rp{};
        rp.renderTarget = m_sceneRT;
        RenderPassAttachmentOp colorOp{};
        colorOp.loadOp = LoadOp::Clear;
        colorOp.storeOp = StoreOp::Store;
        colorOp.clearValue.color[0] = 0.02f;
        colorOp.clearValue.color[1] = 0.02f;
        colorOp.clearValue.color[2] = 0.03f;
        colorOp.clearValue.color[3] = 1.0f;
        rp.colorOps.push_back(colorOp);

        RenderPassAttachmentOp velOp{};
        velOp.loadOp = LoadOp::Clear;
        velOp.storeOp = StoreOp::Store;
        rp.colorOps.push_back(velOp);

        rp.hasDepthStencil = true;
        rp.depthStencilOp.loadOp = LoadOp::Clear;
        rp.depthStencilOp.storeOp = StoreOp::DontCare;
        rp.depthStencilOp.clearValue.depth = 1.0f;
        rp.depthStencilOp.clearValue.stencil = 0;

        cmd.BeginRenderPass(rp);

        Viewport vp{};
        vp.width = static_cast<float>(m_width);
        vp.height = static_cast<float>(m_height);
        cmd.SetViewport(vp);
        Rect2D sc{};
        sc.width = m_width;
        sc.height = m_height;
        cmd.SetScissor(sc);

        cmd.BindPipeline(m_scenePipeline);

        {
            ResourceSetDesc rs{};

            ResourceBindingValue scene{};
            scene.binding = 0;
            scene.type = ResourceBindingType::UniformBuffer;
            scene.buffer = m_sceneUbo;
            scene.bufferOffset = 0;
            scene.bufferRange = sizeof(TAASceneBlock);
            rs.bindings.push_back(scene);

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

    const uint32_t histRead = 1u - m_historyWrite;
    const bool useHistory = m_historyValid && m_history[histRead].IsValid();
    const TextureHandle histTex = useHistory ? m_history[histRead] : m_currColor;

    if (m_resolveUbo.IsValid())
    {
        ZoneScopedN("TAA::UpdateResolve");
        TAAResolveBlock data{};
        data.params = TitusMath::Vec4(
            m_ctx->taaFeedback,
            static_cast<float>(m_ctx->taaClampMode),
            useHistory ? 1.0f : 0.0f,
            0.0f);
        data.jitter = TitusMath::Vec4(jitter.x, jitter.y, 0.0f, 0.0f);
        device.UpdateBuffer(m_resolveUbo, &data, sizeof(data), 0);
    }

    {
        ZoneScopedN("TAA::Resolve");

        RenderPassBeginInfo rp{};
        rp.renderTarget = m_historyRT[m_historyWrite];
        RenderPassAttachmentOp colorOp{};
        colorOp.loadOp = LoadOp::DontCare;
        colorOp.storeOp = StoreOp::Store;
        rp.colorOps.push_back(colorOp);
        rp.hasDepthStencil = false;

        cmd.BeginRenderPass(rp);

        Viewport vp{};
        vp.width = static_cast<float>(m_width);
        vp.height = static_cast<float>(m_height);
        cmd.SetViewport(vp);
        Rect2D sc{};
        sc.width = m_width;
        sc.height = m_height;
        cmd.SetScissor(sc);

        cmd.BindPipeline(m_resolvePipeline);

        {
            ResourceSetDesc rs{};

            ResourceBindingValue curr{};
            curr.binding = 0;
            curr.type = ResourceBindingType::CombinedImageSampler;
            curr.texture = m_currColor;
            curr.sampler = m_pointSampler;
            rs.bindings.push_back(curr);

            ResourceBindingValue vel{};
            vel.binding = 1;
            vel.type = ResourceBindingType::CombinedImageSampler;
            vel.texture = m_velocity;
            vel.sampler = m_pointSampler;
            rs.bindings.push_back(vel);

            ResourceBindingValue hist{};
            hist.binding = 2;
            hist.type = ResourceBindingType::CombinedImageSampler;
            hist.texture = histTex;
            hist.sampler = m_linearSampler;
            rs.bindings.push_back(hist);

            ResourceBindingValue params{};
            params.binding = 3;
            params.type = ResourceBindingType::UniformBuffer;
            params.buffer = m_resolveUbo;
            params.bufferOffset = 0;
            params.bufferRange = sizeof(TAAResolveBlock);
            rs.bindings.push_back(params);

            cmd.BindResourceSet(0, rs);
        }

        cmd.Draw(3);
        cmd.EndRenderPass();
    }

    {
        ZoneScopedN("TAA::Blit");

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

        cmd.BeginRenderPass(rp);

        Viewport vp{};
        vp.width = static_cast<float>(vpW);
        vp.height = static_cast<float>(vpH);
        cmd.SetViewport(vp);
        Rect2D sc{};
        sc.width = vpW;
        sc.height = vpH;
        cmd.SetScissor(sc);

        cmd.BindPipeline(m_blitPipeline);

        {
            ResourceSetDesc rs{};
            ResourceBindingValue tex{};
            tex.binding = 0;
            tex.type = ResourceBindingType::CombinedImageSampler;
            tex.texture = m_history[m_historyWrite];
            tex.sampler = m_pointSampler;
            rs.bindings.push_back(tex);
            cmd.BindResourceSet(0, rs);
        }

        cmd.Draw(3);
        cmd.EndRenderPass();
    }

    m_prevViewProj = currViewProj;
    m_hasPrevViewProj = true;
    m_historyValid = true;
    m_historyWrite = 1u - m_historyWrite;
    ++m_jitterIndex;
}
