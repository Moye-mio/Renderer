// ============================================================================
// 006_Dynamic_Diffuse_GI - SponzaGBufferPass.cpp
// ============================================================================
#include "SponzaGBufferPass.h"
#include "Sponza.h"

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
    if (m_height == 0) m_height = 1080;

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
    m_albedoTex = makeColorRT("DDGI.GBuffer.Albedo");
    m_normalTex = makeColorRT("DDGI.GBuffer.Normal");
    m_positionTex = makeColorRT("DDGI.GBuffer.Position");

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::D32_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::DepthStencilAttachment | TextureUsage::Sampled;
        td.debugName = "DDGI.GBuffer.Depth";
        m_depthTex = device.CreateTexture(td);
    }

    {
        RenderTargetDesc rt{};
        rt.width = m_width;
        rt.height = m_height;
        rt.colorAttachments.push_back({m_albedoTex, 0, 0});
        rt.colorAttachments.push_back({m_normalTex, 0, 0});
        rt.colorAttachments.push_back({m_positionTex, 0, 0});
        rt.depthStencilAttachment = {m_depthTex, 0, 0};
        rt.debugName = "DDGI.GBuffer.RT";
        m_renderTarget = device.CreateRenderTarget(rt);
    }

    const std::string shaderDir = std::string(SOLUTION_DIR) + "006_Dynamic_Diffuse_GI/Shader/";
    std::vector<uint8_t> vsBytes, fsBytes;
    if (TitusAsset::ReadAllBytes(shaderDir + "Sponza_VS.glsl", vsBytes)
        && TitusAsset::ReadAllBytes(shaderDir + "Sponza_GBuffer_FS.glsl", fsBytes))
    {
        ShaderDesc vsDesc{};
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.code = vsBytes.data();
        vsDesc.bytes = vsBytes.size();
        vsDesc.entryPoint = "main";
        vsDesc.debugName = "DDGI.GBuffer.VS";
        m_vs = device.CreateShader(vsDesc);

        ShaderDesc fsDesc{};
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.code = fsBytes.data();
        fsDesc.bytes = fsBytes.size();
        fsDesc.entryPoint = "main";
        fsDesc.debugName = "DDGI.GBuffer.FS";
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
        if (m_sponza)
        {
            pd.vertexLayout = GetMeshSharedLayout(m_sponza->GetModelHandle());
            if (pd.vertexLayout.attributes.size() > 3)
                pd.vertexLayout.attributes.resize(3);
        }
        pd.blend.attachments.resize(3);
        pd.rtLayout.colorFormats = {
            Format::R32G32B32A32_SFLOAT,
            Format::R32G32B32A32_SFLOAT,
            Format::R32G32B32A32_SFLOAT
        };
        pd.rtLayout.depthStencilFormat = Format::D32_SFLOAT;

        PushConstantRange pcModel{};
        pcModel.stages = ShaderStage::Vertex;
        pcModel.offset = 0;
        pcModel.size = sizeof(TitusMath::Mat4);
        pcModel.glName = "u_ModelMatrix";
        pd.pushConstantRanges.push_back(pcModel);

        ResourceBinding rb{};
        rb.name = "u_Matrices4ProjectionWorld";
        rb.set = 0;
        rb.binding = 0;
        rb.type = ResourceBindingType::UniformBuffer;
        rb.stages = ShaderStage::Vertex;
        pd.resourceBindings.push_back(rb);

        ResourceBinding rbDiff{};
        rbDiff.name = "u_DiffuseTexture";
        rbDiff.set = 0;
        rbDiff.binding = 1;
        rbDiff.type = ResourceBindingType::CombinedImageSampler;
        rbDiff.stages = ShaderStage::Fragment;
        pd.resourceBindings.push_back(rbDiff);

        pd.debugName = "DDGI.GBuffer.Pipeline";
        m_pipeline = device.CreatePipeline(pd);
    }
    else
    {
        LOG_STREAM_ERROR("SponzaGBufferPass") << "shader files missing";
    }

    RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("AlbedoTexture", m_albedoTex);
    RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("NormalTexture", m_normalTex);
    RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("PositionTexture", m_positionTex);
    RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("DepthTexture", m_depthTex);
}

void SponzaGBufferPass::Destroy(TitusRHI::IGDevice& device)
{
    for (auto& b : m_matricesUbos)
        if (b.IsValid()) device.Destroy(b);
    m_matricesUbos.clear();
    if (m_pipeline.IsValid()) device.Destroy(m_pipeline);
    if (m_fs.IsValid()) device.Destroy(m_fs);
    if (m_vs.IsValid()) device.Destroy(m_vs);
    if (m_renderTarget.IsValid()) device.Destroy(m_renderTarget);
    if (m_depthTex.IsValid()) device.Destroy(m_depthTex);
    if (m_positionTex.IsValid()) device.Destroy(m_positionTex);
    if (m_normalTex.IsValid()) device.Destroy(m_normalTex);
    if (m_albedoTex.IsValid()) device.Destroy(m_albedoTex);
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

    RenderPassBeginInfo rp{};
    rp.renderTarget = m_renderTarget;
    rp.renderArea.width = m_width;
    rp.renderArea.height = m_height;

    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
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

    if (m_pipeline.IsValid())
        cmd.BindPipeline(m_pipeline);

    {
        ZoneScopedN("DDGI.GBuffer.Matrices");
        const uint32_t frameSlot = device.GetCurrentFrameIndex();
        while (m_matricesUbos.size() <= frameSlot)
        {
            BufferDesc bd{};
            bd.size = sizeof(TitusMath::Mat4) * 2;
            bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
            bd.memory = MemoryUsage::CpuToGpu;
            bd.debugName = "DDGI.GBuffer.Matrices";
            m_matricesUbos.push_back(device.CreateBuffer(bd));
        }
        const BufferHandle matricesUbo = m_matricesUbos[frameSlot];

        TitusMath::Mat4 mats[2] = {
            CAMERA::GetMainCameraProjectionMatrix(),
            CAMERA::GetMainCameraViewMatrix()
        };
        device.UpdateBuffer(matricesUbo, mats, sizeof(mats), 0);

        ResourceSetDesc rs{};
        ResourceBindingValue bv{};
        bv.binding = 0;
        bv.type = ResourceBindingType::UniformBuffer;
        bv.buffer = matricesUbo;
        bv.bufferOffset = 0;
        bv.bufferRange = sizeof(mats);
        rs.bindings.push_back(bv);
        cmd.BindResourceSet(0, rs);
    }

    if (m_sponza)
    {
        ZoneScopedN("DDGI.GBuffer.Draw");
        const TitusMath::Mat4 model = m_sponza->GetModelMatrix();
        cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);
        DrawGpuModelWithDiffuse(cmd, m_sponza->GetModelHandle(), 0, 1);
    }

    cmd.EndRenderPass();
}
