#pragma once
// ============================================================================
// 003_Toon_Shading - SceneDraw
// 着色 UBO、管线绑定与 Cel-Ramp 逐 SubMesh 绘制。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"
#include "NilouMaterials.h"
#include "TechniqueContext.h"

#include <cmath>

struct ToonShadingUBO
{
    TitusMath::Mat4 projection{1.0f};
    TitusMath::Mat4 view{1.0f};
    TitusMath::Vec4 lightDirVSAndAmbient{0.0f, 1.0f, 0.0f, 0.22f};
    TitusMath::Vec4 lightColor{1.0f, 0.96f, 0.88f, 0.0f};
    // x=brightFac y=greyFac z=darkFac  w=0 DiffuseOnly / 1 day / 2 night
    TitusMath::Vec4 rampParams{0.52f, 0.47f, 0.12f, 1.0f};
};
static_assert(sizeof(ToonShadingUBO) == 176, "ToonShadingUBO std140 size");

struct OutlineUBO
{
    TitusMath::Mat4 projection{1.0f};
    TitusMath::Mat4 view{1.0f};
    TitusMath::Vec4 params{0.003f, 0.0f, 0.0f, 0.0f}; // x=widthNdc y=zBias
    TitusMath::Vec4 color{0.06f, 0.04f, 0.07f, 1.0f};
};
static_assert(sizeof(OutlineUBO) == 160, "OutlineUBO std140 size");

struct ToonNprGpu
{
    TitusRHI::TextureHandle bodyIlm{};
    TitusRHI::TextureHandle hairIlm{};
    TitusRHI::TextureHandle faceIlm{};
    TitusRHI::TextureHandle bodyRamp{};
    TitusRHI::TextureHandle hairRamp{};
    TitusRHI::SamplerHandle ilmSampler{};
    TitusRHI::SamplerHandle rampSampler{};
    TitusRHI::BufferHandle  shadingUbo{};
};

inline TitusMath::Vec3 LightDirFromYawPitch(float yawDeg, float pitchDeg)
{
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float yaw = yawDeg * kDegToRad;
    const float pitch = pitchDeg * kDegToRad;
    const float cp = std::cos(pitch);
    return TitusMath::normalize(TitusMath::Vec3(
        cp * std::cos(yaw),
        std::sin(pitch),
        cp * std::sin(yaw)));
}

inline void FillToonShadingUBO(ToonShadingUBO& data, const TechniqueContext* ctx)
{
    data.projection = TitusRHI::CAMERA::GetMainCameraProjectionMatrix();
    data.view = TitusRHI::CAMERA::GetMainCameraViewMatrix();
    const float yaw = ctx ? ctx->lightYawDeg : 8.0f;
    const float pitch = ctx ? ctx->lightPitchDeg : 22.0f;
    const float ambient = ctx ? ctx->ambient : 0.08f;
    const TitusMath::Vec3 lightDirWS = LightDirFromYawPitch(yaw, pitch);
    const TitusMath::Vec4 lightDirVS = data.view * TitusMath::Vec4(lightDirWS, 0.0f);
    data.lightDirVSAndAmbient = TitusMath::Vec4(TitusMath::Vec3(lightDirVS), ambient);
    data.lightColor = TitusMath::Vec4(1.0f, 0.96f, 0.88f, 0.0f);

    const float bright = ctx ? ctx->brightFac : 0.52f;
    const float grey   = ctx ? ctx->greyFac   : 0.47f;
    const float dark   = ctx ? ctx->darkFac   : 0.12f;
    float mode = 0.0f;
    if (ctx && ctx->mode == ToonTechnique::CelRamp)
        mode = ctx->nightRamp ? 2.0f : 1.0f;
    data.rampParams = TitusMath::Vec4(bright, grey, dark, mode);
}

inline void FillOutlineUBO(OutlineUBO& data, const TechniqueContext* ctx)
{
    data.projection = TitusRHI::CAMERA::GetMainCameraProjectionMatrix();
    data.view = TitusRHI::CAMERA::GetMainCameraViewMatrix();
    const float pixels = ctx ? ctx->outlinePixels : 2.5f;
    const float zBias = ctx ? ctx->outlineZBias : 0.0f;
    const float fbW = static_cast<float>(TitusRHI::WINDOW_KEYWORD::GetWindowWidth());
    const float widthNdc = pixels * 2.0f / (fbW > 1.0f ? fbW : 1920.0f);
    data.params = TitusMath::Vec4(widthNdc, zBias, 0.0f, 0.0f);
    const TitusMath::Vec3 c = ctx ? ctx->outlineColor
                                  : TitusMath::Vec3{0.06f, 0.04f, 0.07f};
    data.color = TitusMath::Vec4(c, 1.0f);
}

inline void FillToonPipelineDesc(TitusRHI::GraphicsPipelineDesc& pd,
                                 TitusRHI::ShaderHandle vs,
                                 TitusRHI::ShaderHandle fs,
                                 TitusRHI::GpuModelHandle layoutSource)
{
    using namespace TitusRHI;
    pd.vertexShader = vs;
    pd.fragmentShader = fs;
    pd.topology = PrimitiveTopology::TriangleList;
    pd.rasterizer.cullMode = CullMode::Back;
    pd.rasterizer.frontFace = FrontFace::CounterClockwise;
    pd.depthStencil.depthTestEnable = true;
    pd.depthStencil.depthWriteEnable = true;
    pd.depthStencil.depthCompareOp = CompareOp::Less;
    pd.blend.attachments.resize(1);
    if (layoutSource.IsValid())
        pd.vertexLayout = GetMeshSharedLayout(layoutSource);

    PushConstantRange pcModel{};
    pcModel.stages = ShaderStage::Vertex;
    pcModel.offset = 0;
    pcModel.size = sizeof(TitusMath::Mat4);
    pcModel.glName = "u_ModelMatrix";
    pd.pushConstantRanges.push_back(pcModel);

    ResourceBinding rbUbo{};
    rbUbo.name = "u_ToonShading";
    rbUbo.set = 0;
    rbUbo.binding = 0;
    rbUbo.type = ResourceBindingType::UniformBuffer;
    rbUbo.stages = ShaderStage::Vertex | ShaderStage::Fragment;
    pd.resourceBindings.push_back(rbUbo);

    const char* samplerNames[] = {
        "u_DiffuseTexture", "u_IlmTexture", "u_RampTexture"
    };
    for (uint32_t i = 0; i < 3; ++i)
    {
        ResourceBinding rb{};
        rb.name = samplerNames[i];
        rb.set = 0;
        rb.binding = 1 + i;
        rb.type = ResourceBindingType::CombinedImageSampler;
        rb.stages = ShaderStage::Fragment;
        pd.resourceBindings.push_back(rb);
    }
}

inline void FillOutlinePipelineDesc(TitusRHI::GraphicsPipelineDesc& pd,
                                    TitusRHI::ShaderHandle vs,
                                    TitusRHI::ShaderHandle fs,
                                    TitusRHI::GpuModelHandle layoutSource)
{
    using namespace TitusRHI;
    pd.vertexShader = vs;
    pd.fragmentShader = fs;
    pd.topology = PrimitiveTopology::TriangleList;
    pd.rasterizer.cullMode = CullMode::Front;
    pd.rasterizer.frontFace = FrontFace::CounterClockwise;
    pd.depthStencil.depthTestEnable = true;
    pd.depthStencil.depthWriteEnable = false;
    pd.depthStencil.depthCompareOp = CompareOp::Less;
    pd.blend.attachments.resize(1);
    if (layoutSource.IsValid())
        pd.vertexLayout = GetMeshSharedLayout(layoutSource);

    PushConstantRange pcModel{};
    pcModel.stages = ShaderStage::Vertex;
    pcModel.offset = 0;
    pcModel.size = sizeof(TitusMath::Mat4);
    pcModel.glName = "u_ModelMatrix";
    pd.pushConstantRanges.push_back(pcModel);

    ResourceBinding rbUbo{};
    rbUbo.name = "u_Outline";
    rbUbo.set = 0;
    rbUbo.binding = 0;
    rbUbo.type = ResourceBindingType::UniformBuffer;
    rbUbo.stages = ShaderStage::Vertex | ShaderStage::Fragment;
    pd.resourceBindings.push_back(rbUbo);
}

inline void DrawGpuModelWithCelRamp(TitusRHI::RenderCommandList& cmd,
                                    TitusRHI::GpuModelHandle handle,
                                    const ToonNprGpu& npr)
{
    using namespace TitusRHI;
    ZoneScopedN("DrawGpuModelWithCelRamp");

    const void* p = APP::GetGpuModelInternal(handle);
    if (!p) return;
    const GpuModel* model = static_cast<const GpuModel*>(p);
    const auto& mesh = model->GetMesh();
    const auto& mats = model->GetMaterials();

    TextureHandle lastDiffuse{}, lastIlm{}, lastRamp{};
    SamplerHandle lastDiffSmp{};
    bool hasLast = false;

    ResourceSetDesc rs{};
    rs.bindings.resize(4);

    auto setUbo = [&]()
    {
        ResourceBindingValue& bv = rs.bindings[0];
        bv = ResourceBindingValue{};
        bv.binding = 0;
        bv.type = ResourceBindingType::UniformBuffer;
        bv.buffer = npr.shadingUbo;
        bv.bufferOffset = 0;
        bv.bufferRange = sizeof(ToonShadingUBO);
    };

    auto setCis = [&](uint32_t index, uint32_t binding,
                      TextureHandle tex, SamplerHandle smp)
    {
        ResourceBindingValue& bv = rs.bindings[index];
        bv = ResourceBindingValue{};
        bv.binding = binding;
        bv.type = ResourceBindingType::CombinedImageSampler;
        bv.texture = tex;
        bv.sampler = smp;
    };

    setUbo();

    for (size_t i = 0; i < mesh.subMeshes.size(); ++i)
    {
        const auto& sub = mesh.subMeshes[i];
        if (sub.vertexBuffer.IsValid())
            cmd.BindVertexBuffer(0, sub.vertexBuffer, 0);

        TextureHandle diffuseTex{};
        SamplerHandle diffuseSmp{};
        if (i < mats.size())
        {
            const auto& binding = mats[i].TextureAt(MaterialTextureSlot::Diffuse);
            diffuseTex = binding.texture;
            diffuseSmp = binding.sampler;
        }

        const std::string& key = (i < mats.size() && !mats[i].name.empty())
            ? mats[i].name : sub.name;
        const NilouMaterials::Part part = NilouMaterials::ClassifyPart(key);

        TextureHandle ilm = npr.faceIlm;
        TextureHandle ramp = npr.bodyRamp;
        switch (part)
        {
        case NilouMaterials::Part::Hair:
            ilm = npr.hairIlm;
            ramp = npr.hairRamp;
            break;
        case NilouMaterials::Part::Face:
            ilm = npr.faceIlm;
            ramp = npr.bodyRamp;
            break;
        case NilouMaterials::Part::Dress:
        case NilouMaterials::Part::Body:
        default:
            ilm = npr.bodyIlm;
            ramp = npr.bodyRamp;
            break;
        }

        const bool same = hasLast
            && diffuseTex.id == lastDiffuse.id
            && diffuseSmp.id == lastDiffSmp.id
            && ilm.id == lastIlm.id
            && ramp.id == lastRamp.id;
        if (!same && diffuseTex.IsValid() && ilm.IsValid() && ramp.IsValid())
        {
            setCis(1, 1, diffuseTex, diffuseSmp);
            setCis(2, 2, ilm, npr.ilmSampler);
            setCis(3, 3, ramp, npr.rampSampler);
            cmd.BindResourceSet(0, rs);
            lastDiffuse = diffuseTex;
            lastDiffSmp = diffuseSmp;
            lastIlm = ilm;
            lastRamp = ramp;
            hasLast = true;
        }

        if (sub.indexCount > 0 && sub.indexBuffer.IsValid())
        {
            cmd.BindIndexBuffer(sub.indexBuffer, sub.indexType, 0);
            cmd.DrawIndexed(sub.indexCount, 1, 0, 0, 0);
        }
        else
        {
            cmd.Draw(sub.vertexCount, 1, 0, 0);
        }
    }
}
