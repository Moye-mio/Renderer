// ============================================================================
// 003_Toon_Shading - main.cpp
//
// 卡通渲染对比示例（M2）：妮露角色 + Cel-Ramp（半 Lambert × ilm 采 Ramp）。
// 描边 / 脸 SDF 按 Docs/003_Toon_Shading_Tasks.md 后续里程碑接入。
//
// 架构与 000 / 001 / 002 一致：仅通过 TitusRHI::* 启动；AssetLoader 解码
// CPU IR，业务层按材质名绑 Diffuse，gfx 上传得到 GpuModelHandle。
// 默认 OpenGL，`--backend=vk` 可切 Vulkan。
// ============================================================================
#include <cstdio>
#include <memory>
#include <string>

#include "Logger.h"

#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxPass.h"
#include "RendererInterface/TitusGfxImGui.h"
#include "RendererInterface/TitusGfxOverlay.h"

#include "AssetLoader/AssetTypes.h"
#include "AssetLoader/ModelLoader.h"

#include "NilouMaterials.h"
#include "Scene.h"
#include "TechniqueContext.h"
#include "ToonPass.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

int main(int argc, char** argv)
{
    using namespace TitusRHI;
    const char* kLog = "003_Toon_Shading";

    TitusBasic::Logger::Instance().Init(kLog);

    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown)
        APP::SetBackend(GBackend::OpenGL);
    const char* backendName =
        (APP::GetBackend() == GBackend::OpenGL) ? "OpenGL" :
        (APP::GetBackend() == GBackend::Vulkan) ? "Vulkan" : "Null";
    LOG_STREAM_INFO(kLog) << "backend = " << backendName
        << ", validation = " << (APP::GetEnableValidation() ? "on" : "off");

    WINDOW_KEYWORD::SetWindowSize(1920, 1152);
    WINDOW_KEYWORD::SetIsCursorDisable(false);
    WINDOW_KEYWORD::SetWindowTitle("003_Toon_Shading");
    COMPONENT_CONFIG::SetIsEnableGUI(true);

    APP::InitApp();

    const std::string modelDir  = std::string(SOLUTION_DIR) + "Model/Nilou";
    // 本仓 Assimp 读这套二进制 FBX 会 ACCESS_VIOLATION；几何用 ufbx 转出的 T-pose OBJ。
    const std::string modelPath = modelDir + "/Nilou.obj";

    TitusAsset::ModelLoadOptions modelOpts{};
    modelOpts.loadTextures = false;
    // OBJ vt 与 000/001/002 一样按 OpenGL（v=0 在底）。不要再 FlipUVs：
    // 默认 true 会把 V 再翻一次，采到图集另一侧，看起来像贴图贴错。
    modelOpts.flipUVs = false;
    modelOpts.triangulate = true;
    modelOpts.generateNormals = false;   // OBJ 已带 vn
    modelOpts.calcTangentSpace = false;

    TitusAsset::ModelAssetData modelAsset{};
    if (!TitusAsset::LoadModel(modelPath, modelAsset, modelOpts))
    {
        LOG_STREAM_ERROR(kLog) << "failed to load Nilou: " << modelPath;
        APP::ShutdownApp();
        return 1;
    }

    if (!NilouMaterials::FilterAndBindDiffuse(modelAsset, modelDir))
    {
        LOG_STREAM_ERROR(kLog) << "Nilou material bind failed";
        APP::ShutdownApp();
        return 1;
    }

    TitusMath::Vec3 bbMin, bbMax;
    if (!ComputeModelAabb(modelAsset, bbMin, bbMax))
    {
        LOG_STREAM_ERROR(kLog) << "Nilou AABB invalid";
        APP::ShutdownApp();
        return 1;
    }
    LOG_STREAM_INFO(kLog)
        << "Nilou AABB min=(" << bbMin.x << "," << bbMin.y << "," << bbMin.z << ") "
        << "max=(" << bbMax.x << "," << bbMax.y << "," << bbMax.z << ") "
        << "meshes=" << modelAsset.meshes.size();

    const TitusMath::Mat4 modelMatrix = MakeFitGroundMatrix(bbMin, bbMax, 1.8f);

    GpuModelHandle modelHandle = APP::UploadGpuModel(modelAsset);
    if (!modelHandle.IsValid())
    {
        LOG_STREAM_ERROR(kLog) << "UploadGpuModel failed";
        APP::ShutdownApp();
        return 1;
    }

    {
        CAMERA::FlyCameraConfig cfg{};
        cfg.position  = TitusMath::Vec3{0.0f, 1.05f, 3.4f};
        cfg.yawDeg    = -90.0f;
        cfg.pitchDeg  = -8.0f;
        cfg.fovDeg    = 35.0f;
        cfg.aspect    = 0.0f;
        cfg.nearPlane = 0.05f;
        cfg.farPlane  = 40.0f;
        cfg.moveSpeed = 1.5f;
        CAMERA::SetBuiltinFlyCameraConfig(cfg);
        CAMERA::EnableBuiltinFlyCamera(true);
    }

    {
        Scene scene(modelHandle, modelMatrix);
        TechniqueContext techniqueCtx;
        auto toonPass = std::make_shared<ToonPass>();
        toonPass->SetScene(&scene);
        toonPass->SetContext(&techniqueCtx);
        toonPass->SetTextureDir(modelDir);

        APP::AddPass(toonPass);
        APP::SetScheduledPasses({toonPass});

        OVERLAY::AddPanel("Toon Shading", [&techniqueCtx]()
        {
            int m = static_cast<int>(techniqueCtx.mode);
            ImGui::RadioButton("Diffuse only (M1)", &m, static_cast<int>(ToonTechnique::DiffuseOnly));
            ImGui::SameLine();
            ImGui::RadioButton("Cel-Ramp (M2)", &m, static_cast<int>(ToonTechnique::CelRamp));
            techniqueCtx.mode = static_cast<ToonTechnique>(m);

            ImGui::Separator();
            ImGui::TextUnformatted("Main light");
            ImGui::SliderFloat("Yaw", &techniqueCtx.lightYawDeg, -180.0f, 180.0f);
            ImGui::SliderFloat("Pitch", &techniqueCtx.lightPitchDeg, -10.0f, 89.0f);
            ImGui::SliderFloat("Ambient", &techniqueCtx.ambient, 0.0f, 1.0f);

            ImGui::Separator();
            ImGui::TextUnformatted("Ramp thresholds");
            ImGui::SliderFloat("BrightFac", &techniqueCtx.brightFac, 0.20f, 0.95f);
            ImGui::SliderFloat("GreyFac", &techniqueCtx.greyFac, 0.05f, 0.90f);
            ImGui::SliderFloat("DarkFac", &techniqueCtx.darkFac, 0.00f, 0.60f);
            ImGui::Checkbox("Night ramp rows", &techniqueCtx.nightRamp);
        });

        while (!APP::ShouldClose())
            APP::UpdateApp();

        APP::WaitIdle();
        OVERLAY::ClearPanels();
    }

    APP::ShutdownApp();
    return 0;
}
