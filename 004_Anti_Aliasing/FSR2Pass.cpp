// ============================================================================
// 004_Anti_Aliasing - FSR2Pass.cpp
//
// 低分辨率 jitter 前向 MRT → 显示分辨率 Lanczos + 时域累加 → 复用 FSR 1.0 RCAS。
// ============================================================================
#include "FSR2Pass.h"
#include "Sponza.h"
#include "TechniqueContext.h"

#include <algorithm>
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
    constexpr TitusRHI::Format kDepthVSFormat = TitusRHI::Format::R16_SFLOAT;
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

    struct FSR2SceneBlock
    {
        TitusMath::Mat4 projection{1.0f};
        TitusMath::Mat4 view{1.0f};
        TitusMath::Mat4 prevViewProj{1.0f};
        TitusMath::Vec4 jitter{0.0f, 0.0f, 0.0f, 0.0f};
    };
    static_assert(sizeof(FSR2SceneBlock) == 208, "FSR2SceneBlock std140 size");

    struct FSR2AccumBlock
    {
        TitusMath::Vec4 params{0.12f, 1.0f, 0.0f, 0.0f};
        TitusMath::Vec4 jitter{0.0f, 0.0f, 0.0f, 0.0f};
        TitusMath::Vec4 renderSize{1.0f, 1.0f, 1.0f, 1.0f};
        TitusMath::Vec4 displaySize{1.0f, 1.0f, 1.0f, 1.0f};
    };
    static_assert(sizeof(FSR2AccumBlock) == 64, "FSR2AccumBlock std140 size");

    struct RCASBlock
    {
        TitusMath::Vec4 params{0.0f, 1.0f, 1.0f, 0.0f};
    };
    static_assert(sizeof(RCASBlock) == 16, "RCASBlock std140 size");

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

    // Halton(2,3) 映射到渲染分辨率 NDC：一像素 = 2/renderWidth。
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

FSR2Pass::FSR2Pass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void FSR2Pass::ResetHistory()
{
    m_historyValid = false;
    m_hasPrevViewProj = false;
    m_jitterIndex = 0;
    m_historyWrite = 0;
}

void FSR2Pass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    const std::string shaderDir = std::string(SOLUTION_DIR) + "004_Anti_Aliasing/Shader/";

    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "FSR2_Scene_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "FSR2_Scene_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "FSR2Pass.SceneVS";
            m_sceneVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "FSR2Pass.SceneFS";
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
            pd.blend.attachments.resize(3);
            pd.rtLayout.colorFormats = {kColorFormat, kVelocityFormat, kDepthVSFormat};
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
            rbScene.name = "u_FSR2Scene";
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

            pd.debugName = "FSR2Pass.ScenePipeline";
            m_scenePipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("FSR2Pass") << "scene shaders missing";
        }
    }

    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "Blit_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "FSR2_Accumulate_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "FSR2Pass.AccumVS";
            m_accumVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "FSR2Pass.AccumFS";
            m_accumFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_accumVS;
            pd.fragmentShader = m_accumFS;
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

            ResourceBinding rbDepth{};
            rbDepth.name = "u_DepthVS";
            rbDepth.set = 0;
            rbDepth.binding = 2;
            rbDepth.type = ResourceBindingType::CombinedImageSampler;
            rbDepth.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbDepth);

            ResourceBinding rbHist{};
            rbHist.name = "u_History";
            rbHist.set = 0;
            rbHist.binding = 3;
            rbHist.type = ResourceBindingType::CombinedImageSampler;
            rbHist.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbHist);

            ResourceBinding rbParams{};
            rbParams.name = "u_FSR2Params";
            rbParams.set = 0;
            rbParams.binding = 4;
            rbParams.type = ResourceBindingType::UniformBuffer;
            rbParams.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbParams);

            pd.debugName = "FSR2Pass.AccumPipeline";
            m_accumPipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("FSR2Pass") << "accumulate shaders missing";
        }
    }

    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "Blit_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "FSR_RCAS_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "FSR2Pass.RCASVS";
            m_rcasVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "FSR2Pass.RCASFS";
            m_rcasFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_rcasVS;
            pd.fragmentShader = m_rcasFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.depthStencil.depthTestEnable = false;
            pd.depthStencil.depthWriteEnable = false;
            pd.blend.attachments.resize(1);

            ResourceBinding rbColor{};
            rbColor.name = "u_InputColor";
            rbColor.set = 0;
            rbColor.binding = 0;
            rbColor.type = ResourceBindingType::CombinedImageSampler;
            rbColor.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbColor);

            ResourceBinding rbParams{};
            rbParams.name = "u_RCASParams";
            rbParams.set = 0;
            rbParams.binding = 1;
            rbParams.type = ResourceBindingType::UniformBuffer;
            rbParams.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbParams);

            pd.debugName = "FSR2Pass.RCASPipeline";
            m_rcasPipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("FSR2Pass") << "rcas shaders missing";
        }
    }

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "FSR2Pass.PointSampler";
        m_pointSampler = device.CreateSampler(sd);
    }
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Linear;
        sd.magFilter = FilterMode::Linear;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "FSR2Pass.LinearSampler";
        m_linearSampler = device.CreateSampler(sd);
    }

    {
        BufferDesc bd{};
        bd.size = sizeof(FSR2SceneBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FSR2Pass.UBO.Scene";
        m_sceneUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(ShadingBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FSR2Pass.UBO.Shading";
        m_shadingUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(FSR2AccumBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FSR2Pass.UBO.Accum";
        m_accumUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(RCASBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FSR2Pass.UBO.RCAS";
        m_rcasUbo = device.CreateBuffer(bd);
    }

    uint32_t w = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    uint32_t h = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (w == 0) w = 1920;
    if (h == 0) h = 1152;
    const float scale = m_ctx ? m_ctx->fsr2RenderScale : 0.667f;
    EnsureTargets(device,
                  static_cast<uint32_t>(std::lround(w * scale)),
                  static_cast<uint32_t>(std::lround(h * scale)),
                  w, h);
}

void FSR2Pass::DestroyTargets(TitusRHI::IGDevice& device)
{
    if (m_sceneRT.IsValid()) device.Destroy(m_sceneRT);
    if (m_historyRT[0].IsValid()) device.Destroy(m_historyRT[0]);
    if (m_historyRT[1].IsValid()) device.Destroy(m_historyRT[1]);
    if (m_history[0].IsValid()) device.Destroy(m_history[0]);
    if (m_history[1].IsValid()) device.Destroy(m_history[1]);
    if (m_depth.IsValid()) device.Destroy(m_depth);
    if (m_depthVS.IsValid()) device.Destroy(m_depthVS);
    if (m_velocity.IsValid()) device.Destroy(m_velocity);
    if (m_currColor.IsValid()) device.Destroy(m_currColor);
    m_sceneRT = {};
    m_historyRT[0] = {};
    m_historyRT[1] = {};
    m_history[0] = {};
    m_history[1] = {};
    m_depth = {};
    m_depthVS = {};
    m_velocity = {};
    m_currColor = {};
}

void FSR2Pass::EnsureTargets(TitusRHI::IGDevice& device,
                             uint32_t renderWidth, uint32_t renderHeight,
                             uint32_t displayWidth, uint32_t displayHeight)
{
    using namespace TitusRHI;

    if (renderWidth == 0 || renderHeight == 0 || displayWidth == 0 || displayHeight == 0)
        return;
    if (m_renderWidth == renderWidth && m_renderHeight == renderHeight
        && m_displayWidth == displayWidth && m_displayHeight == displayHeight
        && m_sceneRT.IsValid() && m_historyRT[0].IsValid() && m_historyRT[1].IsValid())
        return;

    device.WaitIdle();
    DestroyTargets(device);

    auto makeTex = [&](Format fmt, uint32_t w, uint32_t h, TextureUsage usage, const char* name)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = fmt;
        td.width = w;
        td.height = h;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = usage;
        td.debugName = name;
        return device.CreateTexture(td);
    };

    const TextureUsage colorUsage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
    m_currColor = makeTex(kColorFormat, renderWidth, renderHeight, colorUsage, "FSR2Pass.CurrColor");
    m_velocity = makeTex(kVelocityFormat, renderWidth, renderHeight, colorUsage, "FSR2Pass.Velocity");
    m_depthVS = makeTex(kDepthVSFormat, renderWidth, renderHeight, colorUsage, "FSR2Pass.DepthVS");
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kDepthFormat;
        td.width = renderWidth;
        td.height = renderHeight;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "FSR2Pass.Depth";
        m_depth = device.CreateTexture(td);
    }
    m_history[0] = makeTex(kColorFormat, displayWidth, displayHeight, colorUsage, "FSR2Pass.History0");
    m_history[1] = makeTex(kColorFormat, displayWidth, displayHeight, colorUsage, "FSR2Pass.History1");

    {
        RenderTargetDesc rt{};
        rt.width = renderWidth;
        rt.height = renderHeight;
        rt.colorAttachments.push_back({m_currColor, 0, 0});
        rt.colorAttachments.push_back({m_velocity, 0, 0});
        rt.colorAttachments.push_back({m_depthVS, 0, 0});
        rt.depthStencilAttachment = {m_depth, 0, 0};
        rt.debugName = "FSR2Pass.SceneRT";
        m_sceneRT = device.CreateRenderTarget(rt);
    }
    for (int i = 0; i < 2; ++i)
    {
        RenderTargetDesc rt{};
        rt.width = displayWidth;
        rt.height = displayHeight;
        rt.colorAttachments.push_back({m_history[i], 0, 0});
        rt.debugName = (i == 0) ? "FSR2Pass.HistoryRT0" : "FSR2Pass.HistoryRT1";
        m_historyRT[i] = device.CreateRenderTarget(rt);
    }

    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;
    m_displayWidth = displayWidth;
    m_displayHeight = displayHeight;
    ResetHistory();

    LOG_STREAM_INFO("FSR2Pass") << "FSR2 targets render " << renderWidth << "x" << renderHeight
        << " -> display " << displayWidth << "x" << displayHeight;
}

void FSR2Pass::Destroy(TitusRHI::IGDevice& device)
{
    DestroyTargets(device);
    if (m_rcasUbo.IsValid()) device.Destroy(m_rcasUbo);
    if (m_accumUbo.IsValid()) device.Destroy(m_accumUbo);
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_sceneUbo.IsValid()) device.Destroy(m_sceneUbo);
    if (m_linearSampler.IsValid()) device.Destroy(m_linearSampler);
    if (m_pointSampler.IsValid()) device.Destroy(m_pointSampler);
    if (m_rcasPipeline.IsValid()) device.Destroy(m_rcasPipeline);
    if (m_rcasFS.IsValid()) device.Destroy(m_rcasFS);
    if (m_rcasVS.IsValid()) device.Destroy(m_rcasVS);
    if (m_accumPipeline.IsValid()) device.Destroy(m_accumPipeline);
    if (m_accumFS.IsValid()) device.Destroy(m_accumFS);
    if (m_accumVS.IsValid()) device.Destroy(m_accumVS);
    if (m_scenePipeline.IsValid()) device.Destroy(m_scenePipeline);
    if (m_sceneFS.IsValid()) device.Destroy(m_sceneFS);
    if (m_sceneVS.IsValid()) device.Destroy(m_sceneVS);
    m_rcasUbo = {};
    m_accumUbo = {};
    m_shadingUbo = {};
    m_sceneUbo = {};
    m_linearSampler = {};
    m_pointSampler = {};
    m_rcasPipeline = {};
    m_rcasFS = {};
    m_rcasVS = {};
    m_accumPipeline = {};
    m_accumFS = {};
    m_accumVS = {};
    m_scenePipeline = {};
    m_sceneFS = {};
    m_sceneVS = {};
    m_renderWidth = 0;
    m_renderHeight = 0;
    m_displayWidth = 0;
    m_displayHeight = 0;
    ResetHistory();
}

void FSR2Pass::Record(TitusRHI::IGDevice& device,
                      TitusRHI::RenderCommandList& cmd,
                      uint32_t /*frameIndex*/,
                      uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_ctx || m_ctx->mode != AATechnique::FSR2)
        return;
    if (!m_scenePipeline.IsValid() || !m_accumPipeline.IsValid() || !m_rcasPipeline.IsValid())
        return;

    const uint32_t vpW = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    const uint32_t vpH = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());

    const float scale = std::clamp(m_ctx->fsr2RenderScale, 0.25f, 1.0f);
    const uint32_t renderW = std::max(1u, static_cast<uint32_t>(std::lround(vpW * scale)));
    const uint32_t renderH = std::max(1u, static_cast<uint32_t>(std::lround(vpH * scale)));
    EnsureTargets(device, renderW, renderH, vpW, vpH);

    if (!m_sceneRT.IsValid() || !m_historyRT[0].IsValid() || !m_historyRT[1].IsValid())
        return;

    if (m_ctx->fsr2ResetHistory)
    {
        ResetHistory();
        m_ctx->fsr2ResetHistory = false;
    }

    const TitusMath::Mat4 proj = CAMERA::GetMainCameraProjectionMatrix();
    const TitusMath::Mat4 view = CAMERA::GetMainCameraViewMatrix();
    const TitusMath::Mat4 currViewProj = proj * view;
    const float jitterScale = m_ctx->fsr2JitterScale;
    const TitusMath::Vec2 jitter = HaltonJitterNdc(m_jitterIndex, m_renderWidth, m_renderHeight, jitterScale);

    if (m_sceneUbo.IsValid())
    {
        ZoneScopedN("FSR2::UpdateScene");
        FSR2SceneBlock data{};
        data.projection = proj;
        data.view = view;
        data.prevViewProj = m_hasPrevViewProj ? m_prevViewProj : currViewProj;
        data.jitter = TitusMath::Vec4(jitter.x, jitter.y, 0.0f, 0.0f);
        device.UpdateBuffer(m_sceneUbo, &data, sizeof(data), 0);
    }

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("FSR2::UpdateShading");
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
        ZoneScopedN("FSR2::DrawModel");

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

        RenderPassAttachmentOp depthVSOp{};
        depthVSOp.loadOp = LoadOp::Clear;
        depthVSOp.storeOp = StoreOp::Store;
        depthVSOp.clearValue.color[0] = 1.0e4f;
        rp.colorOps.push_back(depthVSOp);

        rp.hasDepthStencil = true;
        rp.depthStencilOp.loadOp = LoadOp::Clear;
        rp.depthStencilOp.storeOp = StoreOp::DontCare;
        rp.depthStencilOp.clearValue.depth = 1.0f;
        rp.depthStencilOp.clearValue.stencil = 0;

        cmd.BeginRenderPass(rp);

        Viewport vp{};
        vp.width = static_cast<float>(m_renderWidth);
        vp.height = static_cast<float>(m_renderHeight);
        cmd.SetViewport(vp);
        Rect2D sc{};
        sc.width = m_renderWidth;
        sc.height = m_renderHeight;
        cmd.SetScissor(sc);

        cmd.BindPipeline(m_scenePipeline);

        {
            ResourceSetDesc rs{};

            ResourceBindingValue scene{};
            scene.binding = 0;
            scene.type = ResourceBindingType::UniformBuffer;
            scene.buffer = m_sceneUbo;
            scene.bufferOffset = 0;
            scene.bufferRange = sizeof(FSR2SceneBlock);
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

    if (m_accumUbo.IsValid())
    {
        ZoneScopedN("FSR2::UpdateAccum");
        const float inW = static_cast<float>(m_renderWidth);
        const float inH = static_cast<float>(m_renderHeight);
        const float outW = static_cast<float>(m_displayWidth);
        const float outH = static_cast<float>(m_displayHeight);

        FSR2AccumBlock data{};
        data.params = TitusMath::Vec4(
            m_ctx->fsr2Feedback,
            static_cast<float>(m_ctx->fsr2ClampMode),
            useHistory ? 1.0f : 0.0f,
            0.0f);
        data.jitter = TitusMath::Vec4(jitter.x, jitter.y, 0.0f, 0.0f);
        data.renderSize = TitusMath::Vec4(inW, inH, 1.0f / inW, 1.0f / inH);
        data.displaySize = TitusMath::Vec4(outW, outH, 1.0f / outW, 1.0f / outH);
        device.UpdateBuffer(m_accumUbo, &data, sizeof(data), 0);
    }

    {
        ZoneScopedN("FSR2::Accumulate");

        RenderPassBeginInfo rp{};
        rp.renderTarget = m_historyRT[m_historyWrite];
        RenderPassAttachmentOp colorOp{};
        colorOp.loadOp = LoadOp::DontCare;
        colorOp.storeOp = StoreOp::Store;
        rp.colorOps.push_back(colorOp);
        rp.hasDepthStencil = false;

        cmd.BeginRenderPass(rp);

        Viewport vp{};
        vp.width = static_cast<float>(m_displayWidth);
        vp.height = static_cast<float>(m_displayHeight);
        cmd.SetViewport(vp);
        Rect2D sc{};
        sc.width = m_displayWidth;
        sc.height = m_displayHeight;
        cmd.SetScissor(sc);

        cmd.BindPipeline(m_accumPipeline);

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

            ResourceBindingValue depth{};
            depth.binding = 2;
            depth.type = ResourceBindingType::CombinedImageSampler;
            depth.texture = m_depthVS;
            depth.sampler = m_pointSampler;
            rs.bindings.push_back(depth);

            ResourceBindingValue hist{};
            hist.binding = 3;
            hist.type = ResourceBindingType::CombinedImageSampler;
            hist.texture = histTex;
            hist.sampler = m_linearSampler;
            rs.bindings.push_back(hist);

            ResourceBindingValue params{};
            params.binding = 4;
            params.type = ResourceBindingType::UniformBuffer;
            params.buffer = m_accumUbo;
            params.bufferOffset = 0;
            params.bufferRange = sizeof(FSR2AccumBlock);
            rs.bindings.push_back(params);

            cmd.BindResourceSet(0, rs);
        }

        cmd.Draw(3);
        cmd.EndRenderPass();
    }

    if (m_rcasUbo.IsValid())
    {
        ZoneScopedN("FSR2::UpdateRCAS");
        const float sharpness = m_ctx->fsr2EnableRcas
            ? std::exp2(-std::max(m_ctx->fsr2SharpnessStops, 0.0f))
            : 0.0f;

        RCASBlock data{};
        data.params = TitusMath::Vec4(
            sharpness,
            static_cast<float>(m_displayWidth),
            static_cast<float>(m_displayHeight),
            0.0f);
        device.UpdateBuffer(m_rcasUbo, &data, sizeof(data), 0);
    }

    {
        ZoneScopedN("FSR2::RCAS");

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

        cmd.BindPipeline(m_rcasPipeline);

        {
            ResourceSetDesc rs{};

            ResourceBindingValue tex{};
            tex.binding = 0;
            tex.type = ResourceBindingType::CombinedImageSampler;
            tex.texture = m_history[m_historyWrite];
            tex.sampler = m_pointSampler;
            rs.bindings.push_back(tex);

            ResourceBindingValue params{};
            params.binding = 1;
            params.type = ResourceBindingType::UniformBuffer;
            params.buffer = m_rcasUbo;
            params.bufferOffset = 0;
            params.bufferRange = sizeof(RCASBlock);
            rs.bindings.push_back(params);

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
