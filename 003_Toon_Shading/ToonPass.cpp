// ============================================================================
// 003_Toon_Shading - ToonPass.cpp
// ============================================================================
#include "ToonPass.h"
#include "NilouMaterials.h"
#include "Scene.h"
#include "SceneDraw.h"
#include "TechniqueContext.h"

#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "ImageLoader.h"
#include "Logger.h"
#include "TracySupport.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

namespace
{
    TitusRHI::TextureHandle UploadPng(TitusRHI::IGDevice& device,
                                      const std::string& path,
                                      bool srgb,
                                      std::string& nameStore)
    {
        using namespace TitusRHI;
        TitusAsset::ImageLoadOptions opts{};
        opts.flipVerticallyOnLoad = true;
        opts.isSRGBHint = srgb;
        opts.desiredChannels = 4;
        auto img = TitusAsset::LoadImage2D(path, opts);
        if (!img || img->mips.empty())
        {
            LOG_STREAM_ERROR("ToonPass") << "failed to load NPR texture: " << path;
            return {};
        }

        nameStore = path;
        TextureDesc desc{};
        desc.type = TextureType::Tex2D;
        desc.format = srgb ? Format::R8G8B8A8_SRGB : Format::R8G8B8A8_UNORM;
        desc.width = img->width;
        desc.height = img->height;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.samples = 1;
        desc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
        desc.debugName = nameStore.c_str();

        TextureHandle tex = device.CreateTexture(desc);
        if (!tex.IsValid())
        {
            LOG_STREAM_ERROR("ToonPass") << "CreateTexture failed: " << path;
            return {};
        }

        const auto& mip = img->mips.front();
        TextureUploadDesc up{};
        up.data = mip.pixels.data();
        up.bytes = mip.pixels.size();
        up.mipLevel = 0;
        up.arrayLayer = 0;
        up.width = mip.width;
        up.height = mip.height;
        up.depth = 1;
        device.UpdateTexture(tex, up);

        LOG_STREAM_INFO("ToonPass")
            << "NPR tex " << path << " " << img->width << "x" << img->height
            << (srgb ? " sRGB" : " linear");
        return tex;
    }

    TitusRHI::TextureHandle CreateFaceIlmStub(TitusRHI::IGDevice& device,
                                              std::string& nameStore)
    {
        using namespace TitusRHI;
        nameStore = "ToonPass.FaceIlmStub";
        TextureDesc desc{};
        desc.type = TextureType::Tex2D;
        desc.format = Format::R8G8B8A8_UNORM;
        desc.width = 1;
        desc.height = 1;
        desc.mipLevels = 1;
        desc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
        desc.debugName = nameStore.c_str();
        TextureHandle tex = device.CreateTexture(desc);
        if (!tex.IsValid())
            return {};

        // G=1 无死阴影，A=1 走 Body Ramp 皮肤行。脸 SDF 后续再接。
        const uint8_t px[4] = { 0, 255, 0, 255 };
        TextureUploadDesc up{};
        up.data = px;
        up.bytes = sizeof(px);
        up.width = 1;
        up.height = 1;
        device.UpdateTexture(tex, up);
        return tex;
    }
}

ToonPass::ToonPass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

bool ToonPass::CreateShaders(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    const std::string shaderDir = std::string(SOLUTION_DIR) + "003_Toon_Shading/Shader/";
    std::vector<uint8_t> vsBytes, fsBytes;
    std::vector<uint8_t> outlineVsBytes, outlineFsBytes;
    std::vector<uint8_t> creaseVsBytes, creaseFsBytes;
    if (!TitusAsset::ReadAllBytes(shaderDir + "Toon_VS.glsl", vsBytes) ||
        !TitusAsset::ReadAllBytes(shaderDir + "Toon_FS.glsl", fsBytes) ||
        !TitusAsset::ReadAllBytes(shaderDir + "Outline_VS.glsl", outlineVsBytes) ||
        !TitusAsset::ReadAllBytes(shaderDir + "Outline_FS.glsl", outlineFsBytes) ||
        !TitusAsset::ReadAllBytes(shaderDir + "Crease_VS.glsl", creaseVsBytes) ||
        !TitusAsset::ReadAllBytes(shaderDir + "Crease_FS.glsl", creaseFsBytes))
    {
        LOG_STREAM_ERROR("ToonPass") << "shader files missing; pipeline not created";
        return false;
    }

    ShaderDesc vsDesc{};
    vsDesc.stage = ShaderStage::Vertex;
    vsDesc.code = vsBytes.data();
    vsDesc.bytes = vsBytes.size();
    vsDesc.entryPoint = "main";
    vsDesc.debugName = "ToonPass.VS";
    m_vs = device.CreateShader(vsDesc);

    ShaderDesc fsDesc{};
    fsDesc.stage = ShaderStage::Fragment;
    fsDesc.code = fsBytes.data();
    fsDesc.bytes = fsBytes.size();
    fsDesc.entryPoint = "main";
    fsDesc.debugName = "ToonPass.FS";
    m_fs = device.CreateShader(fsDesc);

    ShaderDesc ovsDesc{};
    ovsDesc.stage = ShaderStage::Vertex;
    ovsDesc.code = outlineVsBytes.data();
    ovsDesc.bytes = outlineVsBytes.size();
    ovsDesc.entryPoint = "main";
    ovsDesc.debugName = "ToonPass.OutlineVS";
    m_outlineVs = device.CreateShader(ovsDesc);

    ShaderDesc ofsDesc{};
    ofsDesc.stage = ShaderStage::Fragment;
    ofsDesc.code = outlineFsBytes.data();
    ofsDesc.bytes = outlineFsBytes.size();
    ofsDesc.entryPoint = "main";
    ofsDesc.debugName = "ToonPass.OutlineFS";
    m_outlineFs = device.CreateShader(ofsDesc);

    ShaderDesc cvsDesc{};
    cvsDesc.stage = ShaderStage::Vertex;
    cvsDesc.code = creaseVsBytes.data();
    cvsDesc.bytes = creaseVsBytes.size();
    cvsDesc.entryPoint = "main";
    cvsDesc.debugName = "ToonPass.CreaseVS";
    m_creaseVs = device.CreateShader(cvsDesc);

    ShaderDesc cfsDesc{};
    cfsDesc.stage = ShaderStage::Fragment;
    cfsDesc.code = creaseFsBytes.data();
    cfsDesc.bytes = creaseFsBytes.size();
    cfsDesc.entryPoint = "main";
    cfsDesc.debugName = "ToonPass.CreaseFS";
    m_creaseFs = device.CreateShader(cfsDesc);

    return m_vs.IsValid() && m_fs.IsValid()
        && m_outlineVs.IsValid() && m_outlineFs.IsValid()
        && m_creaseVs.IsValid() && m_creaseFs.IsValid();
}

bool ToonPass::CreatePipeline(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!m_scene) return false;

    GraphicsPipelineDesc pd{};
    FillToonPipelineDesc(pd, m_vs, m_fs, m_scene->GetModelHandle());
    pd.debugName = "ToonPass.CelRamp";
    m_pipeline = device.CreatePipeline(pd);

    GraphicsPipelineDesc outlinePd{};
    FillOutlinePipelineDesc(outlinePd, m_outlineVs, m_outlineFs, m_scene->GetModelHandle());
    outlinePd.debugName = "ToonPass.Outline";
    m_outlinePipeline = device.CreatePipeline(outlinePd);

    GraphicsPipelineDesc creasePd{};
    FillCreasePipelineDesc(creasePd, m_creaseVs, m_creaseFs);
    creasePd.debugName = "ToonPass.Crease";
    m_creasePipeline = device.CreatePipeline(creasePd);

    return m_pipeline.IsValid() && m_outlinePipeline.IsValid()
        && m_creasePipeline.IsValid();
}

bool ToonPass::CreateNprTextures(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (m_textureDir.empty())
    {
        LOG_STREAM_ERROR("ToonPass") << "texture dir empty";
        return false;
    }

    const auto bodyFiles = NilouMaterials::FilesForPart(NilouMaterials::Part::Body);
    const auto hairFiles = NilouMaterials::FilesForPart(NilouMaterials::Part::Hair);

    m_bodyIlm = UploadPng(device, TitusAsset::JoinPath(m_textureDir, bodyFiles.ilm),
                          false, m_bodyIlmName);
    m_hairIlm = UploadPng(device, TitusAsset::JoinPath(m_textureDir, hairFiles.ilm),
                          false, m_hairIlmName);
    m_bodyRamp = UploadPng(device, TitusAsset::JoinPath(m_textureDir, bodyFiles.ramp),
                           true, m_bodyRampName);
    m_hairRamp = UploadPng(device, TitusAsset::JoinPath(m_textureDir, hairFiles.ramp),
                           true, m_hairRampName);
    m_faceIlm = CreateFaceIlmStub(device, m_faceIlmName);

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Linear;
        sd.magFilter = FilterMode::Linear;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::Repeat;
        sd.debugName = "ToonPass.IlmSampler";
        m_ilmSampler = device.CreateSampler(sd);
    }
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Linear;
        sd.magFilter = FilterMode::Linear;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "ToonPass.RampSampler";
        m_rampSampler = device.CreateSampler(sd);
    }

    const bool ok = m_bodyIlm.IsValid() && m_hairIlm.IsValid()
        && m_bodyRamp.IsValid() && m_hairRamp.IsValid()
        && m_faceIlm.IsValid()
        && m_ilmSampler.IsValid() && m_rampSampler.IsValid();
    if (!ok)
        LOG_STREAM_ERROR("ToonPass") << "NPR texture / sampler create failed";
    return ok;
}

bool ToonPass::CreateOffscreenTargets(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    m_rtWidth = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    m_rtHeight = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (m_rtWidth == 0) m_rtWidth = 1920;
    if (m_rtHeight == 0) m_rtHeight = 1152;

    auto makeColor = [&](Format fmt, const char* name)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = fmt;
        td.width = m_rtWidth;
        td.height = m_rtHeight;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = name;
        return device.CreateTexture(td);
    };
    m_sceneColor = makeColor(kToonSceneColorFormat, "ToonPass.SceneColor");
    m_creaseGBuffer = makeColor(kToonCreaseFormat, "ToonPass.CreaseGBuffer");

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kToonSceneDepthFormat;
        td.width = m_rtWidth;
        td.height = m_rtHeight;
        td.usage = TextureUsage::DepthStencilAttachment;
        td.debugName = "ToonPass.SceneDepth";
        m_sceneDepth = device.CreateTexture(td);
    }

    {
        RenderTargetDesc rt{};
        rt.width = m_rtWidth;
        rt.height = m_rtHeight;
        rt.colorAttachments.push_back({m_sceneColor, 0, 0});
        rt.colorAttachments.push_back({m_creaseGBuffer, 0, 0});
        rt.depthStencilAttachment = {m_sceneDepth, 0, 0};
        rt.debugName = "ToonPass.SceneRT";
        m_sceneRT = device.CreateRenderTarget(rt);
    }

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "ToonPass.CreaseSampler";
        m_creaseSampler = device.CreateSampler(sd);
    }

    const bool ok = m_sceneColor.IsValid() && m_creaseGBuffer.IsValid()
        && m_sceneDepth.IsValid() && m_sceneRT.IsValid()
        && m_creaseSampler.IsValid();
    if (!ok)
        LOG_STREAM_ERROR("ToonPass") << "offscreen RT create failed";
    return ok;
}

void ToonPass::DestroyOffscreenTargets(TitusRHI::IGDevice& device)
{
    if (m_creaseSampler.IsValid()) device.Destroy(m_creaseSampler);
    if (m_sceneRT.IsValid()) device.Destroy(m_sceneRT);
    if (m_sceneDepth.IsValid()) device.Destroy(m_sceneDepth);
    if (m_creaseGBuffer.IsValid()) device.Destroy(m_creaseGBuffer);
    if (m_sceneColor.IsValid()) device.Destroy(m_sceneColor);
    m_creaseSampler = {};
    m_sceneRT = {};
    m_sceneDepth = {};
    m_creaseGBuffer = {};
    m_sceneColor = {};
}

void ToonPass::DestroyNprTextures(TitusRHI::IGDevice& device)
{
    if (m_rampSampler.IsValid()) device.Destroy(m_rampSampler);
    if (m_ilmSampler.IsValid()) device.Destroy(m_ilmSampler);
    if (m_faceIlm.IsValid()) device.Destroy(m_faceIlm);
    if (m_hairRamp.IsValid()) device.Destroy(m_hairRamp);
    if (m_bodyRamp.IsValid()) device.Destroy(m_bodyRamp);
    if (m_hairIlm.IsValid()) device.Destroy(m_hairIlm);
    if (m_bodyIlm.IsValid()) device.Destroy(m_bodyIlm);
    m_rampSampler = {};
    m_ilmSampler = {};
    m_faceIlm = {};
    m_hairRamp = {};
    m_bodyRamp = {};
    m_hairIlm = {};
    m_bodyIlm = {};
}

void ToonPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    if (!CreateShaders(device) || !CreatePipeline(device)
        || !CreateOffscreenTargets(device) || !CreateNprTextures(device))
        return;

    BufferDesc bd{};
    bd.size = sizeof(ToonShadingUBO);
    bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
    bd.memory = MemoryUsage::CpuToGpu;
    bd.debugName = "ToonPass.UBO.Shading";
    m_shadingUbo = device.CreateBuffer(bd);

    BufferDesc obd{};
    obd.size = sizeof(OutlineUBO);
    obd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
    obd.memory = MemoryUsage::CpuToGpu;
    obd.debugName = "ToonPass.UBO.Outline";
    m_outlineUbo = device.CreateBuffer(obd);

    BufferDesc cbd{};
    cbd.size = sizeof(CreaseUBO);
    cbd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
    cbd.memory = MemoryUsage::CpuToGpu;
    cbd.debugName = "ToonPass.UBO.Crease";
    m_creaseUbo = device.CreateBuffer(cbd);
}

void ToonPass::Destroy(TitusRHI::IGDevice& device)
{
    DestroyNprTextures(device);
    DestroyOffscreenTargets(device);
    if (m_creaseUbo.IsValid()) device.Destroy(m_creaseUbo);
    if (m_outlineUbo.IsValid()) device.Destroy(m_outlineUbo);
    if (m_shadingUbo.IsValid()) device.Destroy(m_shadingUbo);
    if (m_creasePipeline.IsValid()) device.Destroy(m_creasePipeline);
    if (m_outlinePipeline.IsValid()) device.Destroy(m_outlinePipeline);
    if (m_pipeline.IsValid()) device.Destroy(m_pipeline);
    if (m_creaseFs.IsValid()) device.Destroy(m_creaseFs);
    if (m_creaseVs.IsValid()) device.Destroy(m_creaseVs);
    if (m_outlineFs.IsValid()) device.Destroy(m_outlineFs);
    if (m_outlineVs.IsValid()) device.Destroy(m_outlineVs);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    m_creaseUbo = {};
    m_outlineUbo = {};
    m_shadingUbo = {};
    m_creasePipeline = {};
    m_outlinePipeline = {};
    m_pipeline = {};
    m_creaseFs = {};
    m_creaseVs = {};
    m_outlineFs = {};
    m_outlineVs = {};
    m_fs = {};
    m_vs = {};
}

void ToonPass::Record(TitusRHI::IGDevice& device,
                      TitusRHI::RenderCommandList& cmd,
                      uint32_t /*frameIndex*/,
                      uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;
    if (!m_scene || !m_pipeline.IsValid() || !m_shadingUbo.IsValid()
        || !m_sceneRT.IsValid() || !m_creasePipeline.IsValid() || !m_creaseUbo.IsValid())
        return;

    {
        ZoneScopedN("ToonPass::UpdateShading");
        ToonShadingUBO data{};
        FillToonShadingUBO(data, m_ctx);
        device.UpdateBuffer(m_shadingUbo, &data, sizeof(data), 0);
    }

    const uint32_t vpW = m_rtWidth;
    const uint32_t vpH = m_rtHeight;
    Viewport vp{};
    vp.width = static_cast<float>(vpW);
    vp.height = static_cast<float>(vpH);
    Rect2D sc{};
    sc.width = vpW;
    sc.height = vpH;
    const TitusMath::Mat4 model = m_scene->GetModelMatrix();

    {
        ZoneScopedN("ToonPass::Draw");
        RenderPassBeginInfo rp{};
        rp.renderTarget = m_sceneRT;
        rp.renderArea.width = vpW;
        rp.renderArea.height = vpH;

        // 两路都清 0。GL 的 glClear 只读 colorOps[0]，没法给 G-Buffer 单独清；
        // 背景灰在 Crease_FS 里按 viewZ==0 补上。
        RenderPassAttachmentOp colorOp{};
        colorOp.loadOp = LoadOp::Clear;
        colorOp.storeOp = StoreOp::Store;
        colorOp.clearValue.color[0] = 0.0f;
        colorOp.clearValue.color[1] = 0.0f;
        colorOp.clearValue.color[2] = 0.0f;
        colorOp.clearValue.color[3] = 0.0f;
        rp.colorOps.push_back(colorOp);
        rp.colorOps.push_back(colorOp);

        rp.hasDepthStencil = true;
        rp.depthStencilOp.loadOp = LoadOp::Clear;
        rp.depthStencilOp.storeOp = StoreOp::Store;
        rp.depthStencilOp.clearValue.depth = 1.0f;
        rp.depthStencilOp.clearValue.stencil = 0;

        cmd.BeginRenderPass(rp);
        cmd.SetViewport(vp);
        cmd.SetScissor(sc);
        cmd.BindPipeline(m_pipeline);
        cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);

        ToonNprGpu npr{};
        npr.bodyIlm = m_bodyIlm;
        npr.hairIlm = m_hairIlm;
        npr.faceIlm = m_faceIlm;
        npr.bodyRamp = m_bodyRamp;
        npr.hairRamp = m_hairRamp;
        npr.ilmSampler = m_ilmSampler;
        npr.rampSampler = m_rampSampler;
        npr.shadingUbo = m_shadingUbo;
        DrawGpuModelWithCelRamp(cmd, m_scene->GetModelHandle(), npr);

        const bool drawOutline = m_ctx && m_ctx->enableOutline
            && m_ctx->outlinePixels > 0.0f
            && m_outlinePipeline.IsValid() && m_outlineUbo.IsValid();
        if (drawOutline)
        {
            ZoneScopedN("ToonPass::Outline");
            OutlineUBO outlineData{};
            FillOutlineUBO(outlineData, m_ctx);
            device.UpdateBuffer(m_outlineUbo, &outlineData, sizeof(outlineData), 0);

            cmd.BindPipeline(m_outlinePipeline);
            cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);

            ResourceSetDesc rs{};
            ResourceBindingValue ubo{};
            ubo.binding = 0;
            ubo.type = ResourceBindingType::UniformBuffer;
            ubo.buffer = m_outlineUbo;
            ubo.bufferOffset = 0;
            ubo.bufferRange = sizeof(OutlineUBO);
            rs.bindings.push_back(ubo);
            cmd.BindResourceSet(0, rs);

            DrawGpuModel(cmd, m_scene->GetModelHandle());
        }
        cmd.EndRenderPass();
    }

    {
        ZoneScopedN("ToonPass::Crease");
        CreaseUBO creaseData{};
        FillCreaseUBO(creaseData, m_ctx);
        device.UpdateBuffer(m_creaseUbo, &creaseData, sizeof(creaseData), 0);

        RenderPassBeginInfo rp{};
        RenderPassAttachmentOp colorOp{};
        colorOp.loadOp = LoadOp::Clear;
        colorOp.storeOp = StoreOp::Store;
        colorOp.clearValue.color[0] = 0.12f;
        colorOp.clearValue.color[1] = 0.13f;
        colorOp.clearValue.color[2] = 0.16f;
        colorOp.clearValue.color[3] = 1.0f;
        rp.colorOps.push_back(colorOp);
        rp.hasDepthStencil = true;
        rp.depthStencilOp.loadOp = LoadOp::Clear;
        rp.depthStencilOp.storeOp = StoreOp::DontCare;
        rp.depthStencilOp.clearValue.depth = 1.0f;
        rp.renderArea.width = 0;
        rp.renderArea.height = 0;

        cmd.BeginRenderPass(rp);
        cmd.SetViewport(vp);
        cmd.SetScissor(sc);
        cmd.BindPipeline(m_creasePipeline);

        ResourceSetDesc rs{};
        ResourceBindingValue ubo{};
        ubo.binding = 0;
        ubo.type = ResourceBindingType::UniformBuffer;
        ubo.buffer = m_creaseUbo;
        ubo.bufferOffset = 0;
        ubo.bufferRange = sizeof(CreaseUBO);
        rs.bindings.push_back(ubo);

        ResourceBindingValue color{};
        color.binding = 1;
        color.type = ResourceBindingType::CombinedImageSampler;
        color.texture = m_sceneColor;
        color.sampler = m_creaseSampler;
        rs.bindings.push_back(color);

        ResourceBindingValue gbuf{};
        gbuf.binding = 2;
        gbuf.type = ResourceBindingType::CombinedImageSampler;
        gbuf.texture = m_creaseGBuffer;
        gbuf.sampler = m_creaseSampler;
        rs.bindings.push_back(gbuf);

        cmd.BindResourceSet(0, rs);
        cmd.Draw(3);
        cmd.EndRenderPass();
    }
}
