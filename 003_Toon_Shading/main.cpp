// ============================================================================
// 003_Toon_Shading - main.cpp
//
// 卡通渲染对比示例：妮露 + Cel-Ramp，以及背面外扩描边。
// 脸 SDF 后续接入。
//
// 架构与 000 / 001 / 002 一致：仅通过 TitusRHI::* 启动；AssetLoader 解码
// CPU IR，业务层按材质名绑 Diffuse，gfx 上传得到 GpuModelHandle。
// 默认 OpenGL，`--backend=vk` 可切 Vulkan。
// ============================================================================
#include <cstdio>
#include <cstring>
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
#include "OutlineBake.h"
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

    ToonTechnique startMode = ToonTechnique::CelRamp;
    for (int i = 1; i < argc; ++i)
    {
        const char* a = argv[i];
        if (std::strncmp(a, "--mode=", 7) != 0)
            continue;
        const char* v = a + 7;
        if (std::strcmp(v, "diffuse") == 0 || std::strcmp(v, "DiffuseOnly") == 0)
            startMode = ToonTechnique::DiffuseOnly;
        else if (std::strcmp(v, "celramp") == 0 || std::strcmp(v, "CelRamp") == 0)
            startMode = ToonTechnique::CelRamp;
        else if (std::strcmp(v, "outline") == 0 || std::strcmp(v, "Outline") == 0)
            startMode = ToonTechnique::CelRamp;
        else
            LOG_STREAM_ERROR(kLog) << "Unknown --mode value: " << v
                << " (expected diffuse|celramp|outline)";
    }
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

    // 必须在 UploadGpuModel 之前：描边壳要用的平滑法线与 partIndex 直接写进顶点。
    OutlineBake::BakeOutlineAttributes(modelAsset);

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
        cfg.position  = TitusMath::Vec3{0.0f, 1.35f, 3.4f};
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
        techniqueCtx.mode = startMode;
        auto toonPass = std::make_shared<ToonPass>();
        toonPass->SetScene(&scene);
        toonPass->SetContext(&techniqueCtx);
        toonPass->SetTextureDir(modelDir);

        APP::AddPass(toonPass);
        APP::SetScheduledPasses({toonPass});

        OVERLAY::AddPanel("Toon Shading", [&techniqueCtx]()
        {
            int m = static_cast<int>(techniqueCtx.mode);
            ImGui::RadioButton("Diffuse only", &m, static_cast<int>(ToonTechnique::DiffuseOnly));
            ImGui::SameLine();
            ImGui::RadioButton("Cel-Ramp", &m, static_cast<int>(ToonTechnique::CelRamp));
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

            ImGui::Separator();
            ImGui::Checkbox("Crease (screen space)", &techniqueCtx.enableCrease);
            if (ImGui::CollapsingHeader("Crease params"))
            {
                ImGui::SliderFloat("CreasePx", &techniqueCtx.creasePixels, 0.0f, 6.0f);
                ImGui::SliderFloat("NThresh", &techniqueCtx.creaseNormalThresh, 0.02f, 0.60f);
                ImGui::SliderFloat("NSoft", &techniqueCtx.creaseNormalSoft, 0.01f, 0.40f);
                ImGui::SliderFloat("ZThresh", &techniqueCtx.creaseDepthThresh, 0.005f, 0.20f);
                ImGui::SliderFloat("ZSoft", &techniqueCtx.creaseDepthSoft, 0.005f, 0.20f);
                ImGui::TextUnformatted("Width / fade / part color follow Outline params.");
            }

            ImGui::Separator();
            ImGui::Checkbox("Outline (inverted hull)", &techniqueCtx.enableOutline);
            if (ImGui::CollapsingHeader("Outline params"))
            {
                ImGui::SliderFloat("BasePx", &techniqueCtx.outlinePixels, 0.0f, 8.0f);
                ImGui::SliderFloat("MinPx", &techniqueCtx.outlineMinPixels, 0.0f, 4.0f);
                ImGui::SliderFloat("MaxPx", &techniqueCtx.outlineMaxPixels, 0.5f, 16.0f);
                ImGui::SliderFloat("ZBias(m)", &techniqueCtx.outlineZBias, 0.0f, 0.05f);

                ImGui::TextUnformatted("Distance falloff");
                ImGui::SliderFloat("RefDistance", &techniqueCtx.outlineRefDistance, 0.5f, 20.0f);
                ImGui::SliderFloat("FalloffPower", &techniqueCtx.outlineFalloffPower, 0.05f, 3.0f);

                ImGui::TextUnformatted("Distance fade");
                ImGui::SliderFloat("FadeStart", &techniqueCtx.outlineFadeStart, 0.0f, 40.0f);
                ImGui::SliderFloat("FadeEnd", &techniqueCtx.outlineFadeEnd, 0.0f, 40.0f);
                ImGui::SliderFloat("FadeStrength", &techniqueCtx.outlineFadeStrength, 0.0f, 1.0f);
                ImGui::ColorEdit3("FadeColor", &techniqueCtx.outlineFadeColor.x);

                // 顺序与 NilouMaterials::Part 一致。线宽倍率为 0 即该部件不描边。
                ImGui::TextUnformatted("Per part color / width");
                static const char* kPartNames[4] = {"Body", "Dress", "Hair", "Face"};
                for (int i = 0; i < 4; ++i)
                {
                    ImGui::PushID(i);
                    ImGui::ColorEdit3(kPartNames[i], &techniqueCtx.outlinePartColor[i].x);
                    ImGui::SameLine();
                    ImGui::PushItemWidth(90.0f);
                    ImGui::SliderFloat("##w", &techniqueCtx.outlinePartWidth[i], 0.0f, 2.0f);
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                }
            }
        });

        while (!APP::ShouldClose())
            APP::UpdateApp();

        APP::WaitIdle();
        OVERLAY::ClearPanels();
    }

    APP::ShutdownApp();
    return 0;
}
