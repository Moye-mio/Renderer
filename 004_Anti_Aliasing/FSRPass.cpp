// ============================================================================
// 004_Anti_Aliasing - FSRPass.cpp
//
// 低分辨率前向几何 → EASU 上采样到显示分辨率 → RCAS 锐化写回 backbuffer。
// ============================================================================
#include "FSRPass.h"
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
    constexpr TitusRHI::Format kColorFormat = TitusRHI::Format::R8G8B8A8_UNORM;
    constexpr TitusRHI::Format kDepthFormat = TitusRHI::Format::D32_SFLOAT;

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

    struct EASUBlock
    {
        TitusMath::Vec4 con0{1.0f, 1.0f, 0.0f, 0.0f};
        TitusMath::Vec4 inputSize{1.0f, 1.0f, 1.0f, 1.0f};
        TitusMath::Vec4 outputSize{1.0f, 1.0f, 1.0f, 1.0f};
        TitusMath::Vec4 mode{1.0f, 0.0f, 0.0f, 0.0f};
    };
    static_assert(sizeof(EASUBlock) == 64, "EASUBlock std140 size");

    struct RCASBlock
    {
        TitusMath::Vec4 params{0.0f, 1.0f, 1.0f, 0.0f};
    };
    static_assert(sizeof(RCASBlock) == 16, "RCASBlock std140 size");

    bool LoadShaderBytes(const std::string& path, std::vector<uint8_t>& out)
    {
        return TitusAsset::ReadAllBytes(path, out);
    }
}

FSRPass::FSRPass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void FSRPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    const std::string shaderDir = std::string(SOLUTION_DIR) + "004_Anti_Aliasing/Shader/";

    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "Scene_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "Scene_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "FSRPass.SceneVS";
            m_sceneVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "FSRPass.SceneFS";
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
            pd.blend.attachments.resize(1);
            pd.rtLayout.colorFormats = {kColorFormat};
            pd.rtLayout.depthStencilFormat = kDepthFormat;

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

            pd.debugName = "FSRPass.ScenePipeline";
            m_scenePipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("FSRPass") << "scene shaders missing";
        }
    }

    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "Blit_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "FSR_EASU_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "FSRPass.EASUVS";
            m_easuVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "FSRPass.EASUFS";
            m_easuFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_easuVS;
            pd.fragmentShader = m_easuFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.depthStencil.depthTestEnable = false;
            pd.depthStencil.depthWriteEnable = false;
            pd.blend.attachments.resize(1);
            pd.rtLayout.colorFormats = {kColorFormat};

            ResourceBinding rbColor{};
            rbColor.name = "u_InputColor";
            rbColor.set = 0;
            rbColor.binding = 0;
            rbColor.type = ResourceBindingType::CombinedImageSampler;
            rbColor.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbColor);

            ResourceBinding rbParams{};
            rbParams.name = "u_EASUParams";
            rbParams.set = 0;
            rbParams.binding = 1;
            rbParams.type = ResourceBindingType::UniformBuffer;
            rbParams.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbParams);

            pd.debugName = "FSRPass.EASUPipeline";
            m_easuPipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("FSRPass") << "easu shaders missing";
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
            vsDesc.debugName = "FSRPass.RCASVS";
            m_rcasVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "FSRPass.RCASFS";
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

            pd.debugName = "FSRPass.RCASPipeline";
            m_rcasPipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("FSRPass") << "rcas shaders missing";
        }
    }

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Linear;
        sd.magFilter = FilterMode::Linear;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "FSRPass.LinearSampler";
        m_linearSampler = device.CreateSampler(sd);
    }

    {
        BufferDesc bd{};
        bd.size = sizeof(TitusMath::Mat4) * 2;
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FSRPass.UBO.Matrices";
        m_matricesUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(ShadingBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FSRPass.UBO.Shading";
        m_shadingUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(EASUBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FSRPass.UBO.EASU";
        m_easuUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(RCASBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FSRPass.UBO.RCAS";
        m_rcasUbo = device.CreateBuffer(bd);
    }

    uint32_t w = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    uint32_t h = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (w == 0) w = 1920;
    if (h == 0) h = 1152;
    const float scale = m_ctx ? m_ctx->fsrRenderScale : 0.667f;
    EnsureTargets(device,
                  static_cast<uint32_t>(std::lround(w * scale)),
                  static_cast<uint32_t>(std::lround(h * scale)),
                  w, h);
}

void FSRPass::DestroyTargets(TitusRHI::IGDevice& device)
{
    if (m_easuRT.IsValid()) device.Destroy(m_easuRT);
    if (m_easuColor.IsValid()) device.Destroy(m_easuColor);
    if (m_sceneRT.IsValid()) device.Destroy(m_sceneRT);
    if (m_sceneDepth.IsValid()) device.Destroy(m_sceneDepth);
    if (m_sceneColor.IsValid()) device.Destroy(m_sceneColor);
    m_easuRT = {};
    m_easuColor = {};
    m_sceneRT = {};
    m_sceneDepth = {};
    m_sceneColor = {};
}

void FSRPass::EnsureTargets(TitusRHI::IGDevice& device,
                            uint32_t renderWidth, uint32_t renderHeight,
                            uint32_t displayWidth, uint32_t displayHeight)
{
    using namespace TitusRHI;

    if (renderWidth == 0 || renderHeight == 0 || displayWidth == 0 || displayHeight == 0)
        return;
    if (m_renderWidth == renderWidth && m_renderHeight == renderHeight
        && m_displayWidth == displayWidth && m_displayHeight == displayHeight
        && m_sceneRT.IsValid() && m_easuRT.IsValid())
        return;

    device.WaitIdle();
    DestroyTargets(device);

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kColorFormat;
        td.width = renderWidth;
        td.height = renderHeight;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = "FSRPass.SceneColor";
        m_sceneColor = device.CreateTexture(td);
    }
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kDepthFormat;
        td.width = renderWidth;
        td.height = renderHeight;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "FSRPass.SceneDepth";
        m_sceneDepth = device.CreateTexture(td);
    }
    {
        RenderTargetDesc rt{};
        rt.width = renderWidth;
        rt.height = renderHeight;
        rt.colorAttachments.push_back({m_sceneColor, 0, 0});
        rt.depthStencilAttachment = {m_sceneDepth, 0, 0};
        rt.debugName = "FSRPass.SceneRT";
        m_sceneRT = device.CreateRenderTarget(rt);
    }

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kColorFormat;
        td.width = displayWidth;
        td.height = displayHeight;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = "FSRPass.EASUColor";
        m_easuColor = device.CreateTexture(td);
    }
    {
        RenderTargetDesc rt{};
        rt.width = displayWidth;
        rt.height = displayHeight;
        rt.colorAttachments.push_back({m_easuColor, 0, 0});
        rt.debugName = "FSRPass.EASURT";
        m_easuRT = device.CreateRenderTarget(rt);
    }

    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;
    m_displayWidth = displayWidth;
    m_displayHeight = displayHeight;

    LOG_STREAM_INFO("FSRPass") << "FSR targets render " << renderWidth << "x" << renderHeight
        << " -> display " << displayWidth << "x" << displayHeight;
}

void FSRPass::Destroy(TitusRHI::IGDevice& device)
{
    DestroyTargets(device);
    if (m_rcasUbo.IsValid()) device.Destroy(m_rcasUbo);
    if (m_easuUbo.IsValid()) device.Destroy(m_easuUbo);
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_matricesUbo.IsValid()) device.Destroy(m_matricesUbo);
    if (m_linearSampler.IsValid()) device.Destroy(m_linearSampler);
    if (m_rcasPipeline.IsValid()) device.Destroy(m_rcasPipeline);
    if (m_rcasFS.IsValid()) device.Destroy(m_rcasFS);
    if (m_rcasVS.IsValid()) device.Destroy(m_rcasVS);
    if (m_easuPipeline.IsValid()) device.Destroy(m_easuPipeline);
    if (m_easuFS.IsValid()) device.Destroy(m_easuFS);
    if (m_easuVS.IsValid()) device.Destroy(m_easuVS);
    if (m_scenePipeline.IsValid()) device.Destroy(m_scenePipeline);
    if (m_sceneFS.IsValid()) device.Destroy(m_sceneFS);
    if (m_sceneVS.IsValid()) device.Destroy(m_sceneVS);
    m_rcasUbo = {};
    m_easuUbo = {};
    m_shadingUbo = {};
    m_matricesUbo = {};
    m_linearSampler = {};
    m_rcasPipeline = {};
    m_rcasFS = {};
    m_rcasVS = {};
    m_easuPipeline = {};
    m_easuFS = {};
    m_easuVS = {};
    m_scenePipeline = {};
    m_sceneFS = {};
    m_sceneVS = {};
    m_renderWidth = 0;
    m_renderHeight = 0;
    m_displayWidth = 0;
    m_displayHeight = 0;
}

void FSRPass::Record(TitusRHI::IGDevice& device,
                     TitusRHI::RenderCommandList& cmd,
                     uint32_t /*frameIndex*/,
                     uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_ctx || m_ctx->mode != AATechnique::FSR)
        return;
    if (!m_scenePipeline.IsValid() || !m_easuPipeline.IsValid() || !m_rcasPipeline.IsValid())
        return;

    const uint32_t vpW = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    const uint32_t vpH = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());

    const float scale = std::clamp(m_ctx->fsrRenderScale, 0.25f, 1.0f);
    const uint32_t renderW = std::max(1u, static_cast<uint32_t>(std::lround(vpW * scale)));
    const uint32_t renderH = std::max(1u, static_cast<uint32_t>(std::lround(vpH * scale)));
    EnsureTargets(device, renderW, renderH, vpW, vpH);

    if (!m_sceneRT.IsValid() || !m_easuRT.IsValid())
        return;

    if (m_matricesUbo.IsValid())
    {
        ZoneScopedN("FSR::UpdateMatrices");
        TitusMath::Mat4 mats[2] = {
            CAMERA::GetMainCameraProjectionMatrix(),
            CAMERA::GetMainCameraViewMatrix()
        };
        device.UpdateBuffer(m_matricesUbo, mats, sizeof(mats), 0);
    }

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("FSR::UpdateShading");
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
        ZoneScopedN("FSR::DrawModel");

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

    if (m_easuUbo.IsValid())
    {
        ZoneScopedN("FSR::UpdateEASU");
        const float inW = static_cast<float>(m_renderWidth);
        const float inH = static_cast<float>(m_renderHeight);
        const float outW = static_cast<float>(m_displayWidth);
        const float outH = static_cast<float>(m_displayHeight);

        // Con0：把输出像素映射回输入连续坐标，zw 是半像素对齐项
        EASUBlock data{};
        data.con0 = TitusMath::Vec4(
            inW / outW,
            inH / outH,
            0.5f * inW / outW - 0.5f,
            0.5f * inH / outH - 0.5f);
        data.inputSize = TitusMath::Vec4(inW, inH, 1.0f / inW, 1.0f / inH);
        data.outputSize = TitusMath::Vec4(outW, outH, 1.0f / outW, 1.0f / outH);
        data.mode = TitusMath::Vec4(static_cast<float>(m_ctx->fsrUpscaleMode), 0.0f, 0.0f, 0.0f);
        device.UpdateBuffer(m_easuUbo, &data, sizeof(data), 0);
    }

    {
        ZoneScopedN("FSR::EASU");

        RenderPassBeginInfo rp{};
        rp.renderTarget = m_easuRT;
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

        cmd.BindPipeline(m_easuPipeline);

        {
            ResourceSetDesc rs{};

            ResourceBindingValue tex{};
            tex.binding = 0;
            tex.type = ResourceBindingType::CombinedImageSampler;
            tex.texture = m_sceneColor;
            tex.sampler = m_linearSampler;
            rs.bindings.push_back(tex);

            ResourceBindingValue params{};
            params.binding = 1;
            params.type = ResourceBindingType::UniformBuffer;
            params.buffer = m_easuUbo;
            params.bufferOffset = 0;
            params.bufferRange = sizeof(EASUBlock);
            rs.bindings.push_back(params);

            cmd.BindResourceSet(0, rs);
        }

        cmd.Draw(3);
        cmd.EndRenderPass();
    }

    if (m_rcasUbo.IsValid())
    {
        ZoneScopedN("FSR::UpdateRCAS");
        // 官方把锐化档位换成线性系数：sharpnessLinear = exp2(-stops)
        const float sharpness = m_ctx->fsrEnableRcas
            ? std::exp2(-std::max(m_ctx->fsrSharpnessStops, 0.0f))
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
        ZoneScopedN("FSR::RCAS");

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
            tex.texture = m_easuColor;
            tex.sampler = m_linearSampler;
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
}
