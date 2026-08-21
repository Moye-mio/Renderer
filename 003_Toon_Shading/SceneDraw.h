#pragma once
// ============================================================================
// 003_Toon_Shading - SceneDraw
// M1 着色 UBO 与管线公共填充。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"
#include "TechniqueContext.h"

#include <cmath>

struct ToonShadingUBO
{
    TitusMath::Mat4 projection{1.0f};
    TitusMath::Mat4 view{1.0f};
    TitusMath::Vec4 lightDirVSAndAmbient{0.0f, 1.0f, 0.0f, 0.22f};
    TitusMath::Vec4 lightColor{1.0f, 0.96f, 0.88f, 0.0f};
};
static_assert(sizeof(ToonShadingUBO) == 160, "ToonShadingUBO std140 size");

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
    const float yaw = ctx ? ctx->lightYawDeg : 35.0f;
    const float pitch = ctx ? ctx->lightPitchDeg : 50.0f;
    const float ambient = ctx ? ctx->ambient : 0.22f;
    const TitusMath::Vec3 lightDirWS = LightDirFromYawPitch(yaw, pitch);
    const TitusMath::Vec4 lightDirVS = data.view * TitusMath::Vec4(lightDirWS, 0.0f);
    data.lightDirVSAndAmbient = TitusMath::Vec4(TitusMath::Vec3(lightDirVS), ambient);
    data.lightColor = TitusMath::Vec4(1.0f, 0.96f, 0.88f, 0.0f);
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

    ResourceBinding rbDiff{};
    rbDiff.name = "u_DiffuseTexture";
    rbDiff.set = 0;
    rbDiff.binding = 1;
    rbDiff.type = ResourceBindingType::CombinedImageSampler;
    rbDiff.stages = ShaderStage::Fragment;
    pd.resourceBindings.push_back(rbDiff);
}
