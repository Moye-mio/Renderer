// ============================================================================
// 004_Anti_Aliasing - FXAAPass.cpp
//
// 前向几何画到离屏 LDR → 全屏 FXAA 写回 backbuffer。
// ============================================================================
#include "FXAAPass.h"
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

    struct FXAAParamsBlock
    {
        TitusMath::Vec4 params{0.75f, 0.166f, 0.0833f, 0.0f};
    };
    static_assert(sizeof(FXAAParamsBlock) == 16, "FXAAParamsBlock std140 size");

    bool LoadShaderBytes(const std::string& path, std::vector<uint8_t>& out)
    {
        return TitusAsset::ReadAllBytes(path, out);
    }
}

FXAAPass::FXAAPass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void FXAAPass::Init(TitusRHI::IGDevice& device)
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
            vsDesc.debugName = "FXAAPass.SceneVS";
            m_sceneVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "FXAAPass.SceneFS";
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

            pd.debugName = "FXAAPass.ScenePipeline";
            m_scenePipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("FXAAPass") << "scene shaders missing";
        }
    }

    {
        std::vector<uint8_t> vsBytes, fsBytes;
        if (LoadShaderBytes(shaderDir + "Blit_VS.glsl", vsBytes)
            && LoadShaderBytes(shaderDir + "FXAA_FS.glsl", fsBytes))
        {
            ShaderDesc vsDesc{};
            vsDesc.stage = ShaderStage::Vertex;
            vsDesc.code = vsBytes.data();
            vsDesc.bytes = vsBytes.size();
            vsDesc.entryPoint = "main";
            vsDesc.debugName = "FXAAPass.FXAAVS";
            m_fxaaVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "FXAAPass.FXAAFS";
            m_fxaaFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_fxaaVS;
            pd.fragmentShader = m_fxaaFS;
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
            rbParams.name = "u_FXAAParams";
            rbParams.set = 0;
            rbParams.binding = 1;
            rbParams.type = ResourceBindingType::UniformBuffer;
            rbParams.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbParams);

            pd.debugName = "FXAAPass.FXAAPipeline";
            m_fxaaPipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("FXAAPass") << "fxaa shaders missing";
        }
    }

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Linear;
        sd.magFilter = FilterMode::Linear;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "FXAAPass.LinearSampler";
        m_linearSampler = device.CreateSampler(sd);
    }

    {
        BufferDesc bd{};
        bd.size = sizeof(TitusMath::Mat4) * 2;
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FXAAPass.UBO.Matrices";
        m_matricesUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(ShadingBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FXAAPass.UBO.Shading";
        m_shadingUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(FXAAParamsBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "FXAAPass.UBO.FXAA";
        m_fxaaUbo = device.CreateBuffer(bd);
    }

    uint32_t w = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    uint32_t h = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (w == 0) w = 1920;
    if (h == 0) h = 1152;
    EnsureTargets(device, w, h);
}

void FXAAPass::DestroyTargets(TitusRHI::IGDevice& device)
{
    if (m_sceneRT.IsValid()) device.Destroy(m_sceneRT);
    if (m_sceneDepth.IsValid()) device.Destroy(m_sceneDepth);
    if (m_sceneColor.IsValid()) device.Destroy(m_sceneColor);
    m_sceneRT = {};
    m_sceneDepth = {};
    m_sceneColor = {};
}

void FXAAPass::EnsureTargets(TitusRHI::IGDevice& device, uint32_t width, uint32_t height)
{
    using namespace TitusRHI;

    if (width == 0 || height == 0)
        return;
    if (m_width == width && m_height == height && m_sceneRT.IsValid())
        return;

    device.WaitIdle();
    DestroyTargets(device);

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kColorFormat;
        td.width = width;
        td.height = height;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = "FXAAPass.SceneColor";
        m_sceneColor = device.CreateTexture(td);
    }
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kDepthFormat;
        td.width = width;
        td.height = height;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "FXAAPass.SceneDepth";
        m_sceneDepth = device.CreateTexture(td);
    }
    {
        RenderTargetDesc rt{};
        rt.width = width;
        rt.height = height;
        rt.colorAttachments.push_back({m_sceneColor, 0, 0});
        rt.depthStencilAttachment = {m_sceneDepth, 0, 0};
        rt.debugName = "FXAAPass.SceneRT";
        m_sceneRT = device.CreateRenderTarget(rt);
    }

    m_width = width;
    m_height = height;

    LOG_STREAM_INFO("FXAAPass") << "FXAA targets " << width << "x" << height;
}

void FXAAPass::Destroy(TitusRHI::IGDevice& device)
{
    DestroyTargets(device);
    if (m_fxaaUbo.IsValid()) device.Destroy(m_fxaaUbo);
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_matricesUbo.IsValid()) device.Destroy(m_matricesUbo);
    if (m_linearSampler.IsValid()) device.Destroy(m_linearSampler);
    if (m_fxaaPipeline.IsValid()) device.Destroy(m_fxaaPipeline);
    if (m_fxaaFS.IsValid()) device.Destroy(m_fxaaFS);
    if (m_fxaaVS.IsValid()) device.Destroy(m_fxaaVS);
    if (m_scenePipeline.IsValid()) device.Destroy(m_scenePipeline);
    if (m_sceneFS.IsValid()) device.Destroy(m_sceneFS);
    if (m_sceneVS.IsValid()) device.Destroy(m_sceneVS);
    m_fxaaUbo = {};
    m_shadingUbo = {};
    m_matricesUbo = {};
    m_linearSampler = {};
    m_fxaaPipeline = {};
    m_fxaaFS = {};
    m_fxaaVS = {};
    m_scenePipeline = {};
    m_sceneFS = {};
    m_sceneVS = {};
    m_width = 0;
    m_height = 0;
}

void FXAAPass::Record(TitusRHI::IGDevice& device,
                      TitusRHI::RenderCommandList& cmd,
                      uint32_t /*frameIndex*/,
                      uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_ctx || m_ctx->mode != AATechnique::FXAA)
        return;
    if (!m_scenePipeline.IsValid() || !m_fxaaPipeline.IsValid())
        return;

    const uint32_t vpW = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    const uint32_t vpH = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    EnsureTargets(device, vpW, vpH);

    if (!m_sceneRT.IsValid() || !m_sceneColor.IsValid())
        return;

    if (m_matricesUbo.IsValid())
    {
        ZoneScopedN("FXAA::UpdateMatrices");
        TitusMath::Mat4 mats[2] = {
            CAMERA::GetMainCameraProjectionMatrix(),
            CAMERA::GetMainCameraViewMatrix()
        };
        device.UpdateBuffer(m_matricesUbo, mats, sizeof(mats), 0);
    }

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("FXAA::UpdateShading");
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
        ZoneScopedN("FXAA::DrawModel");

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

    if (m_fxaaUbo.IsValid())
    {
        ZoneScopedN("FXAA::UpdateParams");
        FXAAParamsBlock data{};
        data.params = TitusMath::Vec4(
            m_ctx->fxaaSubpix,
            m_ctx->fxaaEdgeThreshold,
            m_ctx->fxaaEdgeThresholdMin,
            0.0f);
        device.UpdateBuffer(m_fxaaUbo, &data, sizeof(data), 0);
    }

    {
        ZoneScopedN("FXAA::Resolve");

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

        cmd.BindPipeline(m_fxaaPipeline);

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
            params.buffer = m_fxaaUbo;
            params.bufferOffset = 0;
            params.bufferRange = sizeof(FXAAParamsBlock);
            rs.bindings.push_back(params);

            cmd.BindResourceSet(0, rs);
        }

        cmd.Draw(3);
        cmd.EndRenderPass();
    }
}
