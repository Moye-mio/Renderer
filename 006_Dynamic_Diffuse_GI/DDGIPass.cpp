// ============================================================================
// 006_Dynamic_Diffuse_GI - DDGIPass.cpp
// ============================================================================
#include "DDGIPass.h"
#include "DDGIContext.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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
    // 与 006 全部 shader 里的 DDGIVolume block 逐字段对应（std140，全 vec4/ivec4）。
    struct DDGIVolumeUBO
    {
        float probeOrigin[4];
        float probeSpacing[4];
        int   probeCounts[4];
        float lightDir[4];
        float lightColor[4];
        float skyColor[4];
        int   atlas[4];
        int   frame[4];
        float bounce[4];
        float rayRot[3][4];
    };

    TitusRHI::ShaderHandle LoadShader(TitusRHI::IGDevice& device,
                                      TitusRHI::ShaderStage stage,
                                      const std::string& path,
                                      const char* debugName)
    {
        std::vector<uint8_t> bytes;
        if (!TitusAsset::ReadAllBytes(path, bytes))
        {
            LOG_STREAM_ERROR("DDGIPass") << "missing shader: " << path;
            return {};
        }
        TitusRHI::ShaderDesc sd{};
        sd.stage = stage;
        sd.code = bytes.data();
        sd.bytes = bytes.size();
        sd.entryPoint = "main";
        sd.debugName = debugName;
        return device.CreateShader(sd);
    }

    void AddBinding(std::vector<TitusRHI::ResourceBinding>& out,
                    const char* name, uint32_t binding,
                    TitusRHI::ResourceBindingType type,
                    TitusRHI::ShaderStage stages)
    {
        TitusRHI::ResourceBinding rb{};
        rb.name = name;
        rb.set = 0;
        rb.binding = binding;
        rb.type = type;
        rb.stages = stages;
        out.push_back(rb);
    }

    void ComputeAABB(const TitusAsset::ModelAssetData& model,
                     TitusMath::Vec3& bbMin, TitusMath::Vec3& bbMax)
    {
        bbMin = TitusMath::Vec3(std::numeric_limits<float>::max());
        bbMax = TitusMath::Vec3(-std::numeric_limits<float>::max());
        for (const auto& mesh : model.meshes)
        {
            bbMin = TitusMath::min(bbMin, mesh.aabbMin);
            bbMax = TitusMath::max(bbMax, mesh.aabbMax);
        }
        if (!(bbMin.x < bbMax.x))
        {
            bbMin = TitusMath::Vec3(-4.0f, -2.6f, -6.3f);
            bbMax = TitusMath::Vec3(4.0f, 2.6f, 6.3f);
        }
    }

    // Shoemake：三个 [0,1) 均匀数 → 球面均匀分布的单位四元数 → 旋转矩阵。
    // 每帧换一个，整套 Fibonacci 射线跟着转，仰角分布才不会逐帧固定。
    void RandomRotation(std::mt19937& rng, float out[3][3])
    {
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);
        const float u1 = uni(rng);
        const float u2 = uni(rng);
        const float u3 = uni(rng);
        const float s1 = std::sqrt(1.0f - u1);
        const float s2 = std::sqrt(u1);
        const float t1 = 6.28318530718f * u2;
        const float t2 = 6.28318530718f * u3;
        const float x = s1 * std::sin(t1);
        const float y = s1 * std::cos(t1);
        const float z = s2 * std::sin(t2);
        const float w = s2 * std::cos(t2);

        out[0][0] = 1.0f - 2.0f * (y * y + z * z);
        out[0][1] = 2.0f * (x * y + w * z);
        out[0][2] = 2.0f * (x * z - w * y);
        out[1][0] = 2.0f * (x * y - w * z);
        out[1][1] = 1.0f - 2.0f * (x * x + z * z);
        out[1][2] = 2.0f * (y * z + w * x);
        out[2][0] = 2.0f * (x * z + w * y);
        out[2][1] = 2.0f * (y * z - w * x);
        out[2][2] = 1.0f - 2.0f * (x * x + y * y);
    }
}

DDGIPass::DDGIPass()
{
    passEvent = TitusRHI::ERenderPassEvent::Lighting;
}

void DDGIPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    m_width = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    m_height = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (m_width == 0) m_width = 1920;
    if (m_height == 0) m_height = 1080;

    TitusMath::Vec3 bbMin(-4.0f, -2.6f, -6.3f);
    TitusMath::Vec3 bbMax(4.0f, 2.6f, 6.3f);
    if (m_model)
        ComputeAABB(*m_model, bbMin, bbMax);

    const float pad = 0.22f;
    const TitusMath::Vec3 innerMin(bbMin.x + pad, bbMin.y + pad, bbMin.z + pad);
    const TitusMath::Vec3 innerMax(bbMax.x - pad, bbMax.y - pad, bbMax.z - pad);
    m_probeOrigin = innerMin;
    m_probeSpacing = TitusMath::Vec3(
        (innerMax.x - innerMin.x) / static_cast<float>(std::max(m_gridX - 1, 1)),
        (innerMax.y - innerMin.y) / static_cast<float>(std::max(m_gridY - 1, 1)),
        (innerMax.z - innerMin.z) / static_cast<float>(std::max(m_gridZ - 1, 1)));
    m_probeCount = static_cast<uint32_t>(m_gridX * m_gridY * m_gridZ);
    m_probeTexelSize = static_cast<uint32_t>(kOctRes + 2);
    m_atlasW = static_cast<uint32_t>(m_gridX) * m_probeTexelSize;
    m_atlasH = static_cast<uint32_t>(m_gridY * m_gridZ) * m_probeTexelSize;

    auto makeAtlas = [&](const char* name)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R16G16B16A16_SFLOAT;
        td.width = m_atlasW;
        td.height = m_atlasH;
        td.usage = TextureUsage::Storage | TextureUsage::Sampled;
        td.debugName = name;
        return device.CreateTexture(td);
    };
    m_irradiance[0] = makeAtlas("DDGI.Irradiance.0");
    m_irradiance[1] = makeAtlas("DDGI.Irradiance.1");
    m_distance[0] = makeAtlas("DDGI.Distance.0");
    m_distance[1] = makeAtlas("DDGI.Distance.1");

    {
        // 太阳阴影 mask。即便没有 ray query 也建出来，保证着色 pass 的 binding
        // 始终有效；此时着色端靠 u_Frame.w 判断不去读它。
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R16_SFLOAT;
        td.width = m_width;
        td.height = m_height;
        td.usage = TextureUsage::Storage | TextureUsage::Sampled;
        td.debugName = "DDGI.ShadowMask";
        m_shadowMask = device.CreateTexture(td);
    }

    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Linear;
        sd.magFilter = FilterMode::Linear;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "DDGI.LinearSampler";
        m_linearSampler = device.CreateSampler(sd);
    }
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "DDGI.PointSampler";
        m_pointSampler = device.CreateSampler(sd);
    }

    // 两个 UBO 都按帧槽位惰性创建，见 AcquireFrameBuffer。

    const std::string shaderDir = std::string(SOLUTION_DIR) + "006_Dynamic_Diffuse_GI/Shader/";
    CreateShadePipeline(device, shaderDir);
    CreateProbePipeline(device, shaderDir);

    const GCaps& caps = device.GetCaps();
    const bool wantRT = caps.supportsRayTracing && caps.supportsRayQuery;
    if (wantRT && m_model)
    {
        m_rtReady = m_accel.Build(device, *m_model);
        if (m_rtReady)
        {
            BufferDesc bd{};
            bd.size = sizeof(float) * 4 * static_cast<size_t>(m_probeCount) * static_cast<size_t>(kRaysPerProbe);
            bd.usage = BufferUsage::StorageBuffer;
            bd.memory = MemoryUsage::GpuOnly;
            bd.debugName = "DDGI.RayHits";
            m_rayHits = device.CreateBuffer(bd);
            m_rtReady = m_rayHits.IsValid() && CreateComputePipelines(device, shaderDir);
        }
    }
    else
    {
        LOG_STREAM_WARN("DDGIPass")
            << "Ray Query unavailable (supportsRayTracing=" << caps.supportsRayTracing
            << ", supportsRayQuery=" << caps.supportsRayQuery
            << "). DDGI probe update is skipped; shading falls back to direct light.";
    }

    if (m_ctx)
        m_ctx->rayTracingReady = m_rtReady;

    LOG_STREAM_INFO("DDGIPass")
        << "probe grid " << m_gridX << "x" << m_gridY << "x" << m_gridZ
        << " (" << m_probeCount << "), atlas " << m_atlasW << "x" << m_atlasH
        << ", RT=" << (m_rtReady ? "on" : "off");
}

bool DDGIPass::CreateComputePipelines(TitusRHI::IGDevice& device, const std::string& shaderDir)
{
    using namespace TitusRHI;

    m_traceCS = LoadShader(device, ShaderStage::Compute, shaderDir + "DDGI_Trace_CS.glsl", "DDGI.Trace.CS");
    m_blendCS = LoadShader(device, ShaderStage::Compute, shaderDir + "DDGI_Blend_CS.glsl", "DDGI.Blend.CS");
    m_borderCS = LoadShader(device, ShaderStage::Compute, shaderDir + "DDGI_Border_CS.glsl", "DDGI.Border.CS");
    m_shadowCS = LoadShader(device, ShaderStage::Compute, shaderDir + "DDGI_Shadow_CS.glsl", "DDGI.Shadow.CS");
    if (!m_traceCS.IsValid() || !m_blendCS.IsValid() || !m_borderCS.IsValid() || !m_shadowCS.IsValid())
        return false;

    {
        ComputePipelineDesc cpd{};
        cpd.computeShader = m_traceCS;
        cpd.debugName = "DDGI.Trace.Pipeline";
        AddBinding(cpd.resourceBindings, "u_TLAS", 0, ResourceBindingType::AccelerationStructure, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "DDGIVolume", 1, ResourceBindingType::UniformBuffer, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "RayHits", 2, ResourceBindingType::StorageBuffer, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "Vertices", 3, ResourceBindingType::StorageBuffer, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "Indices", 4, ResourceBindingType::StorageBuffer, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "MeshRanges", 5, ResourceBindingType::StorageBuffer, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_PrevIrradiance", 6, ResourceBindingType::CombinedImageSampler, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_PrevDistance", 7, ResourceBindingType::CombinedImageSampler, ShaderStage::Compute);
        m_tracePipeline = device.CreatePipeline(cpd);
    }
    {
        ComputePipelineDesc cpd{};
        cpd.computeShader = m_shadowCS;
        cpd.debugName = "DDGI.Shadow.Pipeline";
        AddBinding(cpd.resourceBindings, "u_TLAS", 0, ResourceBindingType::AccelerationStructure, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "DDGIVolume", 1, ResourceBindingType::UniformBuffer, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_NormalTexture", 2, ResourceBindingType::CombinedImageSampler, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_PositionTexture", 3, ResourceBindingType::CombinedImageSampler, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_ShadowMask", 4, ResourceBindingType::StorageTexture, ShaderStage::Compute);
        m_shadowPipeline = device.CreatePipeline(cpd);
    }
    {
        ComputePipelineDesc cpd{};
        cpd.computeShader = m_blendCS;
        cpd.debugName = "DDGI.Blend.Pipeline";
        AddBinding(cpd.resourceBindings, "DDGIVolume", 0, ResourceBindingType::UniformBuffer, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "RayHits", 1, ResourceBindingType::StorageBuffer, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_HistoryIrradiance", 2, ResourceBindingType::CombinedImageSampler, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_HistoryDistance", 3, ResourceBindingType::CombinedImageSampler, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_OutIrradiance", 4, ResourceBindingType::StorageTexture, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_OutDistance", 5, ResourceBindingType::StorageTexture, ShaderStage::Compute);
        m_blendPipeline = device.CreatePipeline(cpd);
    }
    {
        ComputePipelineDesc cpd{};
        cpd.computeShader = m_borderCS;
        cpd.debugName = "DDGI.Border.Pipeline";
        AddBinding(cpd.resourceBindings, "DDGIVolume", 0, ResourceBindingType::UniformBuffer, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_Irradiance", 1, ResourceBindingType::StorageTexture, ShaderStage::Compute);
        AddBinding(cpd.resourceBindings, "u_Distance", 2, ResourceBindingType::StorageTexture, ShaderStage::Compute);
        m_borderPipeline = device.CreatePipeline(cpd);
    }

    return m_tracePipeline.IsValid() && m_blendPipeline.IsValid()
        && m_borderPipeline.IsValid() && m_shadowPipeline.IsValid();
}

bool DDGIPass::CreateShadePipeline(TitusRHI::IGDevice& device, const std::string& shaderDir)
{
    using namespace TitusRHI;
    m_blitVS = LoadShader(device, ShaderStage::Vertex, shaderDir + "Blit_VS.glsl", "DDGI.Blit.VS");
    m_shadeFS = LoadShader(device, ShaderStage::Fragment, shaderDir + "DDGI_Shade_FS.glsl", "DDGI.Shade.FS");
    if (!m_blitVS.IsValid() || !m_shadeFS.IsValid())
        return false;

    GraphicsPipelineDesc pd{};
    pd.vertexShader = m_blitVS;
    pd.fragmentShader = m_shadeFS;
    pd.topology = PrimitiveTopology::TriangleList;
    pd.rasterizer.cullMode = CullMode::None;
    pd.depthStencil.depthTestEnable = false;
    pd.depthStencil.depthWriteEnable = false;
    pd.blend.attachments.resize(1);
    AddBinding(pd.resourceBindings, "DDGIVolume", 0, ResourceBindingType::UniformBuffer, ShaderStage::Fragment);
    AddBinding(pd.resourceBindings, "u_AlbedoTexture", 1, ResourceBindingType::CombinedImageSampler, ShaderStage::Fragment);
    AddBinding(pd.resourceBindings, "u_NormalTexture", 2, ResourceBindingType::CombinedImageSampler, ShaderStage::Fragment);
    AddBinding(pd.resourceBindings, "u_PositionTexture", 3, ResourceBindingType::CombinedImageSampler, ShaderStage::Fragment);
    AddBinding(pd.resourceBindings, "u_Irradiance", 4, ResourceBindingType::CombinedImageSampler, ShaderStage::Fragment);
    AddBinding(pd.resourceBindings, "u_Distance", 5, ResourceBindingType::CombinedImageSampler, ShaderStage::Fragment);
    AddBinding(pd.resourceBindings, "u_ShadowMask", 6, ResourceBindingType::CombinedImageSampler, ShaderStage::Fragment);
    pd.debugName = "DDGI.Shade.Pipeline";
    m_shadePipeline = device.CreatePipeline(pd);
    return m_shadePipeline.IsValid();
}

bool DDGIPass::CreateProbePipeline(TitusRHI::IGDevice& device, const std::string& shaderDir)
{
    using namespace TitusRHI;
    m_probeVS = LoadShader(device, ShaderStage::Vertex, shaderDir + "ProbeDebug_VS.glsl", "DDGI.Probe.VS");
    m_probeFS = LoadShader(device, ShaderStage::Fragment, shaderDir + "ProbeDebug_FS.glsl", "DDGI.Probe.FS");
    if (!m_probeVS.IsValid() || !m_probeFS.IsValid())
        return false;

    GraphicsPipelineDesc pd{};
    pd.vertexShader = m_probeVS;
    pd.fragmentShader = m_probeFS;
    pd.topology = PrimitiveTopology::TriangleList;
    pd.rasterizer.cullMode = CullMode::None;
    pd.depthStencil.depthTestEnable = false;
    pd.depthStencil.depthWriteEnable = false;
    pd.blend.attachments.resize(1);

    PushConstantRange pc{};
    pc.stages = ShaderStage::Vertex;
    pc.offset = 0;
    pc.size = sizeof(float);
    pc.glName = "u_ProbeScale";
    pd.pushConstantRanges.push_back(pc);

    AddBinding(pd.resourceBindings, "DDGIVolume", 0, ResourceBindingType::UniformBuffer, ShaderStage::Vertex | ShaderStage::Fragment);
    AddBinding(pd.resourceBindings, "u_Irradiance", 1, ResourceBindingType::CombinedImageSampler, ShaderStage::Fragment);
    AddBinding(pd.resourceBindings, "u_DepthTexture", 2, ResourceBindingType::CombinedImageSampler, ShaderStage::Fragment);
    AddBinding(pd.resourceBindings, "u_Matrices4ProjectionWorld", 3, ResourceBindingType::UniformBuffer, ShaderStage::Vertex);
    pd.debugName = "DDGI.Probe.Pipeline";
    m_probePipeline = device.CreatePipeline(pd);
    return m_probePipeline.IsValid();
}

void DDGIPass::Destroy(TitusRHI::IGDevice& device)
{
    m_accel.Destroy(device);

    auto destroyH = [&](auto& h)
    {
        if (h.IsValid()) device.Destroy(h);
        h = {};
    };
    destroyH(m_probePipeline);
    destroyH(m_probeFS);
    destroyH(m_probeVS);
    destroyH(m_shadePipeline);
    destroyH(m_shadeFS);
    destroyH(m_blitVS);
    destroyH(m_shadowPipeline);
    destroyH(m_borderPipeline);
    destroyH(m_blendPipeline);
    destroyH(m_tracePipeline);
    destroyH(m_shadowCS);
    destroyH(m_borderCS);
    destroyH(m_blendCS);
    destroyH(m_traceCS);
    destroyH(m_rayHits);
    for (auto& b : m_matricesUbos) destroyH(b);
    for (auto& b : m_volumeUbos) destroyH(b);
    m_matricesUbos.clear();
    m_volumeUbos.clear();
    m_matricesUbo = {};
    m_volumeUbo = {};
    destroyH(m_linearSampler);
    destroyH(m_pointSampler);
    destroyH(m_shadowMask);
    destroyH(m_distance[0]);
    destroyH(m_distance[1]);
    destroyH(m_irradiance[0]);
    destroyH(m_irradiance[1]);
    m_rtReady = false;
}

TitusRHI::BufferHandle DDGIPass::AcquireFrameBuffer(TitusRHI::IGDevice& device,
                                                    std::vector<TitusRHI::BufferHandle>& ring,
                                                    uint32_t frameIndex,
                                                    size_t bytes,
                                                    const char* debugName)
{
    using namespace TitusRHI;
    while (ring.size() <= frameIndex)
    {
        BufferDesc bd{};
        bd.size = bytes;
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = debugName;
        ring.push_back(device.CreateBuffer(bd));
    }
    return ring[frameIndex];
}

void DDGIPass::UpdateVolumeUBO(TitusRHI::IGDevice& device)
{
    DDGIVolumeUBO ubo{};
    ubo.probeOrigin[0] = m_probeOrigin.x;
    ubo.probeOrigin[1] = m_probeOrigin.y;
    ubo.probeOrigin[2] = m_probeOrigin.z;
    ubo.probeOrigin[3] = m_ctx ? m_ctx->normalBias : 0.25f;

    ubo.probeSpacing[0] = m_probeSpacing.x;
    ubo.probeSpacing[1] = m_probeSpacing.y;
    ubo.probeSpacing[2] = m_probeSpacing.z;
    ubo.probeSpacing[3] = m_ctx ? m_ctx->maxRayDistance : 18.0f;

    ubo.probeCounts[0] = m_gridX;
    ubo.probeCounts[1] = m_gridY;
    ubo.probeCounts[2] = m_gridZ;
    ubo.probeCounts[3] = kRaysPerProbe;

    // ctx.lightDir 是太阳的照射方向（朝下），shader 的 u_LightDir 要的是
    // 「指向光源」，这里归一化后取反。少了这个取反就只有朝下的面被照亮。
    TitusMath::Vec3 L = m_ctx ? m_ctx->lightDir : TitusMath::Vec3(0.32f, -1.0f, 0.18f);
    const float len = std::sqrt(L.x * L.x + L.y * L.y + L.z * L.z);
    if (len > 1e-5f)
    {
        L.x /= len;
        L.y /= len;
        L.z /= len;
    }
    ubo.lightDir[0] = -L.x;
    ubo.lightDir[1] = -L.y;
    ubo.lightDir[2] = -L.z;
    ubo.lightDir[3] = m_ctx ? m_ctx->lightIntensity : 3.2f;

    const TitusMath::Vec3 lc = m_ctx ? m_ctx->lightColor : TitusMath::Vec3(1.0f, 0.96f, 0.88f);
    ubo.lightColor[0] = lc.x;
    ubo.lightColor[1] = lc.y;
    ubo.lightColor[2] = lc.z;
    ubo.lightColor[3] = m_ctx ? m_ctx->hysteresis : 0.97f;

    const TitusMath::Vec3 sky = m_ctx ? m_ctx->skyColor : TitusMath::Vec3(0.42f, 0.55f, 0.78f);
    ubo.skyColor[0] = sky.x;
    ubo.skyColor[1] = sky.y;
    ubo.skyColor[2] = sky.z;
    ubo.skyColor[3] = m_ctx ? m_ctx->giIntensity : 1.35f;

    ubo.atlas[0] = static_cast<int>(m_atlasW);
    ubo.atlas[1] = static_cast<int>(m_atlasH);
    ubo.atlas[2] = kOctRes;
    ubo.atlas[3] = static_cast<int>(m_probeTexelSize);

    ubo.frame[0] = static_cast<int>(m_frameIndex);
    ubo.frame[1] = m_ctx ? static_cast<int>(m_ctx->viewMode) : 0;
    ubo.frame[2] = (m_ctx && m_ctx->resetAccumulation) ? 1 : (m_frameIndex == 0 ? 1 : 0);
    ubo.frame[3] = (m_rtReady && m_shadowPipeline.IsValid()) ? 1 : 0;

    ubo.bounce[0] = m_ctx ? m_ctx->bounceScale : 0.85f;
    ubo.bounce[1] = 0.0f;
    ubo.bounce[2] = 0.0f;
    ubo.bounce[3] = 0.0f;

    for (int c = 0; c < 3; ++c)
    {
        ubo.rayRot[c][0] = m_rayRotation[c][0];
        ubo.rayRot[c][1] = m_rayRotation[c][1];
        ubo.rayRot[c][2] = m_rayRotation[c][2];
        ubo.rayRot[c][3] = 0.0f;
    }

    device.UpdateBuffer(m_volumeUbo, &ubo, sizeof(ubo), 0);
}

void DDGIPass::RecordProbeUpdate(TitusRHI::RenderCommandList& cmd)
{
    using namespace TitusRHI;
    const int write = m_writeIndex;
    const int read = 1 - m_writeIndex;

    auto barrierCS = [&]()
    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ComputeShader;
        bar.dstStage = PipelineStage::ComputeShader;
        bar.srcGlobalAccess = AccessFlags::ShaderWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    };

    cmd.BindPipeline(m_tracePipeline);
    {
        ResourceSetDesc rs{};
        ResourceBindingValue tlas{};
        tlas.binding = 0;
        tlas.type = ResourceBindingType::AccelerationStructure;
        tlas.accelStruct = m_accel.GetTLAS();
        rs.bindings.push_back(tlas);

        ResourceBindingValue ubo{};
        ubo.binding = 1;
        ubo.type = ResourceBindingType::UniformBuffer;
        ubo.buffer = m_volumeUbo;
        ubo.bufferRange = sizeof(DDGIVolumeUBO);
        rs.bindings.push_back(ubo);

        ResourceBindingValue hits{};
        hits.binding = 2;
        hits.type = ResourceBindingType::StorageBuffer;
        hits.buffer = m_rayHits;
        rs.bindings.push_back(hits);

        ResourceBindingValue verts{};
        verts.binding = 3;
        verts.type = ResourceBindingType::StorageBuffer;
        verts.buffer = m_accel.GetVertexBuffer();
        rs.bindings.push_back(verts);

        ResourceBindingValue inds{};
        inds.binding = 4;
        inds.type = ResourceBindingType::StorageBuffer;
        inds.buffer = m_accel.GetIndexBuffer();
        rs.bindings.push_back(inds);

        ResourceBindingValue ranges{};
        ranges.binding = 5;
        ranges.type = ResourceBindingType::StorageBuffer;
        ranges.buffer = m_accel.GetMeshRangeBuffer();
        rs.bindings.push_back(ranges);

        // 上一帧的 probe 场：命中点回采它得到第二次及以后的弹射。
        ResourceBindingValue prevI{};
        prevI.binding = 6;
        prevI.type = ResourceBindingType::CombinedImageSampler;
        prevI.texture = m_irradiance[read];
        prevI.sampler = m_linearSampler;
        rs.bindings.push_back(prevI);

        ResourceBindingValue prevD{};
        prevD.binding = 7;
        prevD.type = ResourceBindingType::CombinedImageSampler;
        prevD.texture = m_distance[read];
        prevD.sampler = m_linearSampler;
        rs.bindings.push_back(prevD);
        cmd.BindResourceSet(0, rs);
    }
    cmd.Dispatch(m_probeCount, 1, 1);
    barrierCS();

    cmd.BindPipeline(m_blendPipeline);
    {
        ResourceSetDesc rs{};
        ResourceBindingValue ubo{};
        ubo.binding = 0;
        ubo.type = ResourceBindingType::UniformBuffer;
        ubo.buffer = m_volumeUbo;
        ubo.bufferRange = sizeof(DDGIVolumeUBO);
        rs.bindings.push_back(ubo);

        ResourceBindingValue hits{};
        hits.binding = 1;
        hits.type = ResourceBindingType::StorageBuffer;
        hits.buffer = m_rayHits;
        rs.bindings.push_back(hits);

        ResourceBindingValue histI{};
        histI.binding = 2;
        histI.type = ResourceBindingType::CombinedImageSampler;
        histI.texture = m_irradiance[read];
        histI.sampler = m_linearSampler;
        rs.bindings.push_back(histI);

        ResourceBindingValue histD{};
        histD.binding = 3;
        histD.type = ResourceBindingType::CombinedImageSampler;
        histD.texture = m_distance[read];
        histD.sampler = m_linearSampler;
        rs.bindings.push_back(histD);

        ResourceBindingValue outI{};
        outI.binding = 4;
        outI.type = ResourceBindingType::StorageTexture;
        outI.texture = m_irradiance[write];
        rs.bindings.push_back(outI);

        ResourceBindingValue outD{};
        outD.binding = 5;
        outD.type = ResourceBindingType::StorageTexture;
        outD.texture = m_distance[write];
        rs.bindings.push_back(outD);
        cmd.BindResourceSet(0, rs);
    }
    cmd.Dispatch(m_probeCount, 1, 1);
    barrierCS();

    cmd.BindPipeline(m_borderPipeline);
    {
        ResourceSetDesc rs{};
        ResourceBindingValue ubo{};
        ubo.binding = 0;
        ubo.type = ResourceBindingType::UniformBuffer;
        ubo.buffer = m_volumeUbo;
        ubo.bufferRange = sizeof(DDGIVolumeUBO);
        rs.bindings.push_back(ubo);

        ResourceBindingValue irr{};
        irr.binding = 1;
        irr.type = ResourceBindingType::StorageTexture;
        irr.texture = m_irradiance[write];
        rs.bindings.push_back(irr);

        ResourceBindingValue dist{};
        dist.binding = 2;
        dist.type = ResourceBindingType::StorageTexture;
        dist.texture = m_distance[write];
        rs.bindings.push_back(dist);
        cmd.BindResourceSet(0, rs);
    }
    cmd.Dispatch(m_probeCount, 1, 1);

    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ComputeShader;
        bar.dstStage = PipelineStage::FragmentShader;
        bar.srcGlobalAccess = AccessFlags::ShaderWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    }
}

void DDGIPass::RecordShadowMask(TitusRHI::RenderCommandList& cmd)
{
    using namespace TitusRHI;

    TextureHandle normal = RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("NormalTexture");
    TextureHandle position = RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("PositionTexture");
    if (!normal.IsValid() || !position.IsValid() || !m_shadowMask.IsValid())
        return;

    // GBuffer 的颜色附件写完 → compute 采样读。
    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ColorAttachment;
        bar.dstStage = PipelineStage::ComputeShader;
        bar.srcGlobalAccess = AccessFlags::ColorAttachmentWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    }

    cmd.BindPipeline(m_shadowPipeline);
    {
        ResourceSetDesc rs{};
        ResourceBindingValue tlas{};
        tlas.binding = 0;
        tlas.type = ResourceBindingType::AccelerationStructure;
        tlas.accelStruct = m_accel.GetTLAS();
        rs.bindings.push_back(tlas);

        ResourceBindingValue ubo{};
        ubo.binding = 1;
        ubo.type = ResourceBindingType::UniformBuffer;
        ubo.buffer = m_volumeUbo;
        ubo.bufferRange = sizeof(DDGIVolumeUBO);
        rs.bindings.push_back(ubo);

        auto addTex = [&](uint32_t binding, TextureHandle tex)
        {
            ResourceBindingValue bv{};
            bv.binding = binding;
            bv.type = ResourceBindingType::CombinedImageSampler;
            bv.texture = tex;
            bv.sampler = m_pointSampler;
            rs.bindings.push_back(bv);
        };
        addTex(2, normal);
        addTex(3, position);

        ResourceBindingValue mask{};
        mask.binding = 4;
        mask.type = ResourceBindingType::StorageTexture;
        mask.texture = m_shadowMask;
        rs.bindings.push_back(mask);
        cmd.BindResourceSet(0, rs);
    }
    cmd.Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);

    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ComputeShader;
        bar.dstStage = PipelineStage::FragmentShader;
        bar.srcGlobalAccess = AccessFlags::ShaderWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    }
}

void DDGIPass::RecordShading(TitusRHI::RenderCommandList& cmd)
{
    using namespace TitusRHI;

    const int write = m_writeIndex;
    TextureHandle albedo = RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("AlbedoTexture");
    TextureHandle normal = RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("NormalTexture");
    TextureHandle position = RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("PositionTexture");
    TextureHandle depth = RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("DepthTexture");

    const bool drawProbes = m_ctx && m_ctx->showProbes && m_probePipeline.IsValid() && depth.IsValid();
    if (drawProbes)
    {
        // GBuffer render pass 结束时深度停在 DEPTH_STENCIL_ATTACHMENT_OPTIMAL，
        // probe 调试球要把它当纹理采，必须先转成 SHADER_READ_ONLY。
        // 下一帧 GBuffer 的 RP initialLayout 是 UNDEFINED，转回去是隐式的。
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::DepthAttachment;
        bar.dstStage = PipelineStage::FragmentShader;
        TextureBarrier tb{};
        tb.texture = depth;
        tb.oldLayout = TextureLayout::DepthStencilAttachment;
        tb.newLayout = TextureLayout::ShaderReadOnly;
        tb.srcAccess = AccessFlags::DepthWrite;
        tb.dstAccess = AccessFlags::ShaderRead;
        bar.textureBarriers.push_back(tb);
        cmd.PipelineBarrier(bar);
    }

    RenderPassBeginInfo rp{};
    RenderPassAttachmentOp op{};
    op.loadOp = LoadOp::Clear;
    op.storeOp = StoreOp::Store;
    op.clearValue.color[0] = 0.04f;
    op.clearValue.color[1] = 0.05f;
    op.clearValue.color[2] = 0.07f;
    op.clearValue.color[3] = 1.0f;
    rp.colorOps.push_back(op);
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

    if (m_shadePipeline.IsValid())
    {
        cmd.BindPipeline(m_shadePipeline);
        ResourceSetDesc rs{};
        ResourceBindingValue ubo{};
        ubo.binding = 0;
        ubo.type = ResourceBindingType::UniformBuffer;
        ubo.buffer = m_volumeUbo;
        ubo.bufferRange = sizeof(DDGIVolumeUBO);
        rs.bindings.push_back(ubo);

        auto addTex = [&](uint32_t binding, TextureHandle tex, SamplerHandle samp)
        {
            ResourceBindingValue bv{};
            bv.binding = binding;
            bv.type = ResourceBindingType::CombinedImageSampler;
            bv.texture = tex;
            bv.sampler = samp;
            rs.bindings.push_back(bv);
        };
        addTex(1, albedo, m_pointSampler);
        addTex(2, normal, m_pointSampler);
        addTex(3, position, m_pointSampler);
        addTex(4, m_irradiance[write], m_linearSampler);
        addTex(5, m_distance[write], m_linearSampler);
        addTex(6, m_shadowMask, m_pointSampler);
        cmd.BindResourceSet(0, rs);
        cmd.Draw(3);
    }

    if (drawProbes)
    {
        cmd.BindPipeline(m_probePipeline);
        const float scale = m_ctx->probeVisualScale;
        cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(float), &scale);

        ResourceSetDesc rs{};
        ResourceBindingValue ubo{};
        ubo.binding = 0;
        ubo.type = ResourceBindingType::UniformBuffer;
        ubo.buffer = m_volumeUbo;
        ubo.bufferRange = sizeof(DDGIVolumeUBO);
        rs.bindings.push_back(ubo);

        ResourceBindingValue irr{};
        irr.binding = 1;
        irr.type = ResourceBindingType::CombinedImageSampler;
        irr.texture = m_irradiance[write];
        irr.sampler = m_linearSampler;
        rs.bindings.push_back(irr);

        ResourceBindingValue dep{};
        dep.binding = 2;
        dep.type = ResourceBindingType::CombinedImageSampler;
        dep.texture = depth;
        dep.sampler = m_pointSampler;
        rs.bindings.push_back(dep);

        ResourceBindingValue mats{};
        mats.binding = 3;
        mats.type = ResourceBindingType::UniformBuffer;
        mats.buffer = m_matricesUbo;
        mats.bufferRange = sizeof(TitusMath::Mat4) * 2;
        rs.bindings.push_back(mats);
        cmd.BindResourceSet(0, rs);
        cmd.Draw(36, m_probeCount);
    }

    cmd.EndRenderPass();
}

void DDGIPass::Record(TitusRHI::IGDevice& device,
                      TitusRHI::RenderCommandList& cmd,
                      uint32_t /*frameIndex*/,
                      uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;
    ZoneScopedN("DDGIPass");

    // 本帧的 UBO 槽位：只有等到该槽位的 fence 之后 CPU 才会再写它，
    // 所以 GPU 读到的内容在整条命令流里是稳定的。
    const uint32_t frameSlot = device.GetCurrentFrameIndex();
    m_volumeUbo = AcquireFrameBuffer(device, m_volumeUbos, frameSlot,
                                     sizeof(DDGIVolumeUBO), "DDGI.VolumeUBO");
    m_matricesUbo = AcquireFrameBuffer(device, m_matricesUbos, frameSlot,
                                       sizeof(TitusMath::Mat4) * 2, "DDGI.Probe.Matrices");

    if (m_matricesUbo.IsValid())
    {
        TitusMath::Mat4 mats[2] = {
            CAMERA::GetMainCameraProjectionMatrix(),
            CAMERA::GetMainCameraViewMatrix()
        };
        device.UpdateBuffer(m_matricesUbo, mats, sizeof(mats), 0);
    }

    RandomRotation(m_rng, m_rayRotation);
    UpdateVolumeUBO(device);

    if (m_rtReady && m_tracePipeline.IsValid())
    {
        RecordProbeUpdate(cmd);
        RecordShadowMask(cmd);
    }

    RecordShading(cmd);

    if (m_ctx)
        m_ctx->resetAccumulation = false;
    m_writeIndex = 1 - m_writeIndex;
    ++m_frameIndex;
}
