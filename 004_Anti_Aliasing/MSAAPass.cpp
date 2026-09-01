// ============================================================================
// 004_Anti_Aliasing - MSAAPass.cpp
//
// 前向几何画到 MSAA RT → ResolveTexture → 全屏拷回 backbuffer。
// ============================================================================
#include "MSAAPass.h"
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

    uint32_t SanitizeSamples(uint32_t requested, uint32_t maxSamples)
    {
        if (maxSamples < 2)
            return 1;
        uint32_t s = requested < 2 ? 2 : requested;
        if (s > maxSamples)
            s = maxSamples;
        uint32_t p = 1;
        while ((p << 1) <= s)
            p <<= 1;
        return p < 2 ? 2 : p;
    }

    bool LoadShaderBytes(const std::string& path, std::vector<uint8_t>& out)
    {
        return TitusAsset::ReadAllBytes(path, out);
    }
}

MSAAPass::MSAAPass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void MSAAPass::Init(TitusRHI::IGDevice& device)
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
            vsDesc.debugName = "MSAAPass.SceneVS";
            m_sceneVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "MSAAPass.SceneFS";
            m_sceneFS = device.CreateShader(fsDesc);
        }
        else
        {
            LOG_STREAM_ERROR("MSAAPass") << "scene shaders missing";
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
            vsDesc.debugName = "MSAAPass.BlitVS";
            m_blitVS = device.CreateShader(vsDesc);

            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "MSAAPass.BlitFS";
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
            pd.debugName = "MSAAPass.BlitPipeline";
            m_blitPipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("MSAAPass") << "blit shaders missing";
        }
    }

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "MSAAPass.BlitSampler";
        m_blitSampler = device.CreateSampler(sd);
    }

    {
        BufferDesc bd{};
        bd.size = sizeof(TitusMath::Mat4) * 2;
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "MSAAPass.UBO.Matrices";
        m_matricesUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(ShadingBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "MSAAPass.UBO.Shading";
        m_shadingUbo = device.CreateBuffer(bd);
    }

    uint32_t w = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    uint32_t h = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (w == 0) w = 1920;
    if (h == 0) h = 1152;
    const uint32_t samples = SanitizeSamples(
        m_ctx ? m_ctx->msaaSamples : 4,
        device.GetCaps().maxColorSampleCount);
    EnsureTargets(device, w, h, samples);
}

void MSAAPass::DestroyTargets(TitusRHI::IGDevice& device)
{
    if (m_msaaRT.IsValid()) device.Destroy(m_msaaRT);
    if (m_resolveColor.IsValid()) device.Destroy(m_resolveColor);
    if (m_msaaDepth.IsValid()) device.Destroy(m_msaaDepth);
    if (m_msaaColor.IsValid()) device.Destroy(m_msaaColor);
    m_msaaRT = {};
    m_resolveColor = {};
    m_msaaDepth = {};
    m_msaaColor = {};
}

void MSAAPass::CreateScenePipeline(TitusRHI::IGDevice& device, uint32_t samples)
{
    using namespace TitusRHI;

    if (m_scenePipeline.IsValid())
        device.Destroy(m_scenePipeline);
    m_scenePipeline = {};

    if (!m_sceneVS.IsValid() || !m_sceneFS.IsValid())
        return;

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
    pd.rtLayout.samples = samples;

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

    pd.debugName = "MSAAPass.ScenePipeline";
    m_scenePipeline = device.CreatePipeline(pd);
}

void MSAAPass::EnsureTargets(TitusRHI::IGDevice& device, uint32_t width, uint32_t height, uint32_t samples)
{
    using namespace TitusRHI;

    if (width == 0 || height == 0 || samples < 2)
        return;
    if (m_width == width && m_height == height && m_samples == samples && m_msaaRT.IsValid()
        && m_scenePipeline.IsValid())
        return;

    device.WaitIdle();
    DestroyTargets(device);
    if (m_samples != samples || !m_scenePipeline.IsValid())
        CreateScenePipeline(device, samples);

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kColorFormat;
        td.width = width;
        td.height = height;
        td.mipLevels = 1;
        td.samples = samples;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::TransferSrc;
        td.debugName = "MSAAPass.MSAAColor";
        m_msaaColor = device.CreateTexture(td);
    }
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kDepthFormat;
        td.width = width;
        td.height = height;
        td.mipLevels = 1;
        td.samples = samples;
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "MSAAPass.MSAADepth";
        m_msaaDepth = device.CreateTexture(td);
    }
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kColorFormat;
        td.width = width;
        td.height = height;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::TransferDst | TextureUsage::Sampled;
        td.debugName = "MSAAPass.ResolveColor";
        m_resolveColor = device.CreateTexture(td);
    }
    {
        RenderTargetDesc rt{};
        rt.width = width;
        rt.height = height;
        rt.colorAttachments.push_back({m_msaaColor, 0, 0});
        rt.depthStencilAttachment = {m_msaaDepth, 0, 0};
        rt.debugName = "MSAAPass.MSAART";
        m_msaaRT = device.CreateRenderTarget(rt);
    }

    m_width = width;
    m_height = height;
    m_samples = samples;

    LOG_STREAM_INFO("MSAAPass")
        << "MSAA " << samples << "x  " << width << "x" << height;
}

void MSAAPass::Destroy(TitusRHI::IGDevice& device)
{
    DestroyTargets(device);
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_matricesUbo.IsValid()) device.Destroy(m_matricesUbo);
    if (m_blitSampler.IsValid()) device.Destroy(m_blitSampler);
    if (m_blitPipeline.IsValid()) device.Destroy(m_blitPipeline);
    if (m_blitFS.IsValid()) device.Destroy(m_blitFS);
    if (m_blitVS.IsValid()) device.Destroy(m_blitVS);
    if (m_scenePipeline.IsValid()) device.Destroy(m_scenePipeline);
    if (m_sceneFS.IsValid()) device.Destroy(m_sceneFS);
    if (m_sceneVS.IsValid()) device.Destroy(m_sceneVS);
    m_shadingUbo = {};
    m_matricesUbo = {};
    m_blitSampler = {};
    m_blitPipeline = {};
    m_blitFS = {};
    m_blitVS = {};
    m_scenePipeline = {};
    m_sceneFS = {};
    m_sceneVS = {};
    m_width = 0;
    m_height = 0;
    m_samples = 0;
}

void MSAAPass::Record(TitusRHI::IGDevice& device,
                      TitusRHI::RenderCommandList& cmd,
                      uint32_t /*frameIndex*/,
                      uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_ctx || m_ctx->mode != AATechnique::MSAA)
        return;
    if (!m_blitPipeline.IsValid())
        return;

    const uint32_t vpW = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    const uint32_t vpH = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    const uint32_t samples = SanitizeSamples(m_ctx->msaaSamples, device.GetCaps().maxColorSampleCount);
    EnsureTargets(device, vpW, vpH, samples);

    if (!m_scenePipeline.IsValid() || !m_msaaRT.IsValid() || !m_resolveColor.IsValid())
        return;

    if (m_matricesUbo.IsValid())
    {
        ZoneScopedN("MSAA::UpdateMatrices");
        TitusMath::Mat4 mats[2] = {
            CAMERA::GetMainCameraProjectionMatrix(),
            CAMERA::GetMainCameraViewMatrix()
        };
        device.UpdateBuffer(m_matricesUbo, mats, sizeof(mats), 0);
    }

    if (m_shadingUbo.IsValid())
    {
        ZoneScopedN("MSAA::UpdateShading");
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
        ZoneScopedN("MSAA::DrawModel");

        RenderPassBeginInfo rp{};
        rp.renderTarget = m_msaaRT;
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

    {
        ZoneScopedN("MSAA::Resolve");

        PipelineBarrierDesc toTransfer{};
        toTransfer.srcStage = PipelineStage::ColorAttachment;
        toTransfer.dstStage = PipelineStage::Transfer;
        TextureBarrier dstBar{};
        dstBar.texture = m_resolveColor;
        dstBar.oldLayout = TextureLayout::Undefined;
        dstBar.newLayout = TextureLayout::TransferDst;
        dstBar.srcAccess = AccessFlags::None;
        dstBar.dstAccess = AccessFlags::TransferWrite;
        toTransfer.textureBarriers.push_back(dstBar);
        cmd.PipelineBarrier(toTransfer);

        cmd.ResolveTexture(m_msaaColor, m_resolveColor);

        PipelineBarrierDesc toSample{};
        toSample.srcStage = PipelineStage::Transfer;
        toSample.dstStage = PipelineStage::FragmentShader;
        TextureBarrier sampleBar{};
        sampleBar.texture = m_resolveColor;
        sampleBar.oldLayout = TextureLayout::TransferDst;
        sampleBar.newLayout = TextureLayout::ShaderReadOnly;
        sampleBar.srcAccess = AccessFlags::TransferWrite;
        sampleBar.dstAccess = AccessFlags::ShaderRead;
        toSample.textureBarriers.push_back(sampleBar);
        cmd.PipelineBarrier(toSample);
    }

    {
        ZoneScopedN("MSAA::Blit");

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
            tex.texture = m_resolveColor;
            tex.sampler = m_blitSampler;
            rs.bindings.push_back(tex);
            cmd.BindResourceSet(0, rs);
        }

        cmd.Draw(3);
        cmd.EndRenderPass();
    }
}
