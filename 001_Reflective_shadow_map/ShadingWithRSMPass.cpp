// ============================================================================
// 001_Reflective_shadow_map - ShadingWithRSMPass.cpp
// 真正的 Compute Pass。
// ============================================================================
#include "ShadingWithRSMPass.h"

#include <cstdint>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

ShadingWithRSMPass::ShadingWithRSMPass()
{
    passEvent = TitusRHI::ERenderPassEvent::Lighting;
}

void ShadingWithRSMPass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    // ----------------------------------------------------------------------
    // 1) 输出 storage texture（同时作为 sampled，让 ScreenQuadPass 可采样）
    // ----------------------------------------------------------------------
    m_outputWidth = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowWidth());
    m_outputHeight = static_cast<uint32_t>(TitusRHI::WINDOW_KEYWORD::GetWindowHeight());
    if (m_outputWidth == 0) m_outputWidth = 1920;
    if (m_outputHeight == 0) m_outputHeight = 1152;

    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = Format::R32G32B32A32_SFLOAT;
        td.width = m_outputWidth;
        td.height = m_outputHeight;
        td.usage = TextureUsage::Storage | TextureUsage::Sampled;
        td.debugName = "ShadingWithRSMPass.OutputImage";
        m_shadingTexture = device.CreateTexture(td);
    }
    {
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Linear;
        sd.magFilter = FilterMode::Linear;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToBorder;
        sd.debugName = "ShadingWithRSMPass.Sampler";
        m_inputSampler = device.CreateSampler(sd);
    }
    using TitusRHI::TextureHandle;
    TitusRHI::RESOURCE_MANAGER::RegisterSharedData<TextureHandle>("ShadingTexture", m_shadingTexture);

    // ----------------------------------------------------------------------
    // 2) VPL UBO（binding=1）：32 个 vec4，按极坐标随机采样初始化（与原实现等价）
    // ----------------------------------------------------------------------
    {
        std::vector<TitusMath::Vec4> vplSamples;
        vplSamples.reserve(m_cntVPL);
        std::default_random_engine e;
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int i = 0; i < m_cntVPL; ++i)
        {
            float xi1 = u(e);
            float xi2 = u(e);
            vplSamples.emplace_back(
                xi1 * std::sin(2.0f * static_cast<float>(std::numbers::pi) * xi2),
                xi1 * std::cos(2.0f * static_cast<float>(std::numbers::pi) * xi2),
                xi1 * xi1,
                0.0f);
        }
        BufferDesc bd{};
        bd.size = vplSamples.size() * sizeof(TitusMath::Vec4);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.initialData = vplSamples.data();
        bd.debugName = "ShadingWithRSMPass.VPL_UBO";
        m_vplUbo = device.CreateBuffer(bd);
    }

    // ----------------------------------------------------------------------
    // 3) Compute shader & pipeline
    //    VK 后端不再要求 .spv 预编译产物——VKDevice::CreateShaderImpl
    //    会按 magic word 嗅探，对 GLSL 文本自动走 glslang 在线编译。
    // ----------------------------------------------------------------------
    const std::string shaderDir = std::string(SOLUTION_DIR) + "001_Reflective_shadow_map/Shader/";
    const std::string csPath = shaderDir + "ShadingWithRSM_CS.glsl";
    std::vector<uint8_t> csBytes;
    if (TitusAsset::ReadAllBytes(csPath, csBytes))
    {
        ShaderDesc csDesc{};
        csDesc.stage = ShaderStage::Compute;
        csDesc.code = csBytes.data();
        csDesc.bytes = csBytes.size();
        csDesc.entryPoint = "main";
        csDesc.debugName = "ShadingWithRSMPass.CS";
        m_cs = device.CreateShader(csDesc);

        if (m_cs.IsValid())
        {
            ComputePipelineDesc cpd{};
            cpd.computeShader = m_cs;
            cpd.debugName = "ShadingWithRSMPass.ComputePipeline";

            // 资源绑定声明（与 ShadingWithRSM_CS.glsl 中显式 layout(set, binding) 严格对齐）
            // 注意：Vulkan 中 storage image / UBO / sampler 共享同一 set 内的 binding 命名空间，
            // 因此必须给它们分配互不重叠的 binding 槽。布局：
            //   binding=0 : storage image u_OutputImage
            //   binding=1 : VPL UBO
            //   binding=2 : u_Matrices4ProjectionWorld（CS 中实际未使用，保留用于反射对齐）
            //   binding=3..8 : 6 张 sampler2D
            ResourceBinding outImg{};
            outImg.name = "u_OutputImage";
            outImg.set = 0;
            outImg.binding = 0;
            outImg.type = ResourceBindingType::StorageTexture;
            outImg.stages = ShaderStage::Compute;
            cpd.resourceBindings.push_back(outImg);

            ResourceBinding ubo1{};
            ubo1.name = "VPLsSampleCoordsAndWeights";
            ubo1.set = 0;
            ubo1.binding = 1;
            ubo1.type = ResourceBindingType::UniformBuffer;
            ubo1.stages = ShaderStage::Compute;
            cpd.resourceBindings.push_back(ubo1);

            ResourceBinding ubo0{};
            ubo0.name = "u_Matrices4ProjectionWorld";
            ubo0.set = 0;
            ubo0.binding = 2;
            ubo0.type = ResourceBindingType::UniformBuffer;
            ubo0.stages = ShaderStage::Compute;
            cpd.resourceBindings.push_back(ubo0);

            // 6 张 sampler2D：binding 3..8。显式声明让两端都能反射到同一句柄。
            auto addSampler = [&](const char* name, uint32_t binding)
            {
                ResourceBinding rb{};
                rb.name = name;
                rb.set = 0;
                rb.binding = binding;
                rb.type = ResourceBindingType::CombinedImageSampler;
                rb.stages = ShaderStage::Compute;
                cpd.resourceBindings.push_back(rb);
            };
            addSampler("u_AlbedoTexture", 3);
            addSampler("u_NormalTexture", 4);
            addSampler("u_PositionTexture", 5);
            addSampler("u_RSMFluxTexture", 6);
            addSampler("u_RSMNormalTexture", 7);
            addSampler("u_RSMPositionTexture", 8);

            // PushConstants：5 个独立 uniform。
            // GL 端按 glName 反向查找 location，offset 仅对 VK 起作用。
            // 注意 vec3 在 GLSL push_constant 块中按 std430 自然对齐（16B），
            // 因此 u_LightDirInViewSpace 必须位于 offset=80（int 之后空 4B padding），
            // 与 ShadingWithRSM_CS.glsl 中 layout(offset=80) 严格一致。
            auto addPC = [&](const char* name, uint32_t off, uint32_t size)
            {
                PushConstantRange r{};
                r.stages = ShaderStage::Compute;
                r.offset = off;
                r.size = size;
                r.glName = name;
                cpd.pushConstantRanges.push_back(r);
            };
            addPC("u_LightVPMatrixMulInverseCameraViewMatrix", 0,  sizeof(TitusMath::Mat4)); // [0..63]
            addPC("u_MaxSampleRadius",                         64, sizeof(float));     // [64..67]
            addPC("u_RSMSize",                                 68, sizeof(int));       // [68..71]
            addPC("u_VPLNum",                                  72, sizeof(int));       // [72..75]
            // [76..79] 是为了让 vec3 对齐到 16B 边界而插入的 padding
            addPC("u_LightDirInViewSpace",                     80, sizeof(TitusMath::Vec3)); // [80..91]

            m_computePipeline = device.CreatePipeline(cpd);
        }
    }
    else
    {
        LOG_STREAM_ERROR("ShadingWithRSMPass") << "compute shader file missing: " << csPath;
    }

    // ----------------------------------------------------------------------
    // 4) 取共享数据（LightVP、LightDir、RSMResolution）
    // ----------------------------------------------------------------------
    m_lightVP = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<TitusMath::Mat4>("LightVPMatrix");
    TitusMath::Vec3 ldir = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<TitusMath::Vec3>("LightDir");
    m_lightDirHomo = TitusMath::Vec4(ldir, 0.0f);
    m_rsmResolution = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<int>("RSMResolution");
    if (m_rsmResolution <= 0) m_rsmResolution = 256;

    constexpr uint32_t LOCAL_GROUP_SIZE = 16;
    m_groupCountX = (m_outputWidth + LOCAL_GROUP_SIZE - 1) / LOCAL_GROUP_SIZE;
    m_groupCountY = (m_outputHeight + LOCAL_GROUP_SIZE - 1) / LOCAL_GROUP_SIZE;
}

void ShadingWithRSMPass::Destroy(TitusRHI::IGDevice& device)
{
    if (m_computePipeline.IsValid()) device.Destroy(m_computePipeline);
    if (m_cs.IsValid()) device.Destroy(m_cs);
    if (m_vplUbo.IsValid()) device.Destroy(m_vplUbo);
    if (m_inputSampler.IsValid()) device.Destroy(m_inputSampler);
    if (m_shadingTexture.IsValid()) device.Destroy(m_shadingTexture);
    m_computePipeline = {};
    m_cs = {};
    m_vplUbo = {};
    m_inputSampler = {};
    m_shadingTexture = {};
}

void ShadingWithRSMPass::Record(TitusRHI::IGDevice& /*device*/,
                                TitusRHI::RenderCommandList& cmd,
                                uint32_t /*frameIndex*/,
                                uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_computePipeline.IsValid()) return;

    // 取共享 GBuffer / RSM 纹理
    using TitusRHI::TextureHandle;
    TextureHandle albedo = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("AlbedoTexture");
    TextureHandle normal = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("NormalTexture");
    TextureHandle position = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("PositionTexture");
    TextureHandle rsmFlux = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("RSMFluxTexture");
    TextureHandle rsmNorm = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("RSMNormalTexture");
    TextureHandle rsmPos = TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<TextureHandle>("RSMPositionTexture");

        cmd.BindPipeline(m_computePipeline);

        // 资源绑定：storage image + 2 个 UBO + 6 张 sampler（binding 与 GLSL layout 严格对齐）
        {
            ResourceSetDesc rs{};

            // Storage image (binding=0)
            ResourceBindingValue img{};
            img.binding = 0;
            img.type = ResourceBindingType::StorageTexture;
            img.texture = m_shadingTexture;
            rs.bindings.push_back(img);

            // VPL UBO (binding=1)
            ResourceBindingValue ubo{};
            ubo.binding = 1;
            ubo.type = ResourceBindingType::UniformBuffer;
            ubo.buffer = m_vplUbo;
            ubo.bufferOffset = 0;
            ubo.bufferRange = sizeof(TitusMath::Vec4) * static_cast<size_t>(m_cntVPL);
            rs.bindings.push_back(ubo);

            // u_Matrices4ProjectionWorld (binding=2)：CS 中未实际使用，但需占位以满足
            // DescriptorSetLayout 完整性。复用任意已存在的 UBO——这里偷懒共用 m_vplUbo。
            // 联调时若 Validation Layer 报 "binding=2 unbound"，再补一个真正的
            // matrices UBO。
            ResourceBindingValue uboMat{};
            uboMat.binding = 2;
            uboMat.type = ResourceBindingType::UniformBuffer;
            uboMat.buffer = m_vplUbo;   // 占位
            uboMat.bufferOffset = 0;
            uboMat.bufferRange = sizeof(TitusMath::Vec4) * static_cast<size_t>(m_cntVPL);
            rs.bindings.push_back(uboMat);

            // 6 张 sampler2D (binding=3..8)
            auto pushTex = [&](TextureHandle h, uint32_t binding)
            {
                ResourceBindingValue bv{};
                bv.binding = binding;
                bv.type = ResourceBindingType::CombinedImageSampler;
                bv.texture = h;
                bv.sampler = m_inputSampler;
                rs.bindings.push_back(bv);
            };
            pushTex(albedo, 3);
            pushTex(normal, 4);
            pushTex(position, 5);
            pushTex(rsmFlux, 6);
            pushTex(rsmNorm, 7);
            pushTex(rsmPos, 8);

            cmd.BindResourceSet(0, rs);
        }

    // PushConstants：5 个独立 uniform（按 glName 反向 glUniform）。
    // offset 与 ShadingWithRSM_CS.glsl 中 layout(offset=N) 严格一致：
    //   0  : mat4  u_LightVPMatrixMulInverseCameraViewMatrix
    //   64 : float u_MaxSampleRadius
    //   68 : int   u_RSMSize
    //   72 : int   u_VPLNum
    //   80 : vec3  u_LightDirInViewSpace（vec3 std430 16B 对齐，前面留 4B padding）
    {
        const TitusMath::Mat4 viewMat = TitusRHI::CAMERA::GetMainCameraViewMatrix();
        const TitusMath::Mat4 lightVPMulInvView = m_lightVP * TitusMath::inverse(viewMat);
        const TitusMath::Vec3 lightDirInViewSpace = TitusMath::normalize(TitusMath::Vec3(viewMat * m_lightDirHomo));

        cmd.PushConstants(ShaderStage::Compute,  0, sizeof(TitusMath::Mat4), &lightVPMulInvView);
        cmd.PushConstants(ShaderStage::Compute, 64, sizeof(float),     &m_maxSampleRadius);
        cmd.PushConstants(ShaderStage::Compute, 68, sizeof(int),       &m_rsmResolution);
        cmd.PushConstants(ShaderStage::Compute, 72, sizeof(int),       &m_cntVPL);
        cmd.PushConstants(ShaderStage::Compute, 80, sizeof(TitusMath::Vec3), &lightDirInViewSpace);
    }

    cmd.Dispatch(m_groupCountX, m_groupCountY, 1);

    // 屏障：Storage 写入 → Shader 读取（ScreenQuadPass 会以 sampled texture 读）
    {
        PipelineBarrierDesc bar{};
        bar.srcStage = PipelineStage::ComputeShader;
        bar.dstStage = PipelineStage::FragmentShader;
        bar.srcGlobalAccess = AccessFlags::ShaderWrite;
        bar.dstGlobalAccess = AccessFlags::ShaderRead;
        cmd.PipelineBarrier(bar);
    }
}
