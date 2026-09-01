// ============================================================================
// 004_Anti_Aliasing - main.cpp
//
// 抗锯齿对比示例：Sponza 上切换 None / MSAA。
// FXAA / SMAA / TAA 后续按 TechniqueContext::mode 接入。
//
// 架构与 000 / 001 / 003 一致：仅通过 TitusRHI::* 启动；AssetLoader 解码
// CPU IR，gfx 上传得到 GpuModelHandle。默认 OpenGL，`--backend=vk` 可切 Vulkan。
// ============================================================================
#include <cstring>
#include <limits>
#include <memory>
#include <string>

#include "Logger.h"

#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxPass.h"
#include "RendererInterface/TitusGfxImGui.h"
#include "RendererInterface/TitusGfxOverlay.h"

#include "AssetLoader/AssetTypes.h"
#include "AssetLoader/ModelLoader.h"

#include "MSAAPass.h"
#include "ScenePass.h"
#include "Sponza.h"
#include "TechniqueContext.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

int main(int argc, char** argv)
{
    using namespace TitusRHI;
    const char* kLog = "004_Anti_Aliasing";

    TitusBasic::Logger::Instance().Init(kLog);

    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown)
        APP::SetBackend(GBackend::OpenGL);

    AATechnique startMode = AATechnique::None;
    for (int i = 1; i < argc; ++i)
    {
        const char* a = argv[i];
        if (std::strncmp(a, "--mode=", 7) != 0)
            continue;
        const char* v = a + 7;
        if (std::strcmp(v, "none") == 0 || std::strcmp(v, "None") == 0)
            startMode = AATechnique::None;
        else if (std::strcmp(v, "msaa") == 0 || std::strcmp(v, "MSAA") == 0)
            startMode = AATechnique::MSAA;
        else
            LOG_STREAM_ERROR(kLog) << "Unknown --mode value: " << v
                << " (expected none|msaa)";
    }

    const char* backendName =
        (APP::GetBackend() == GBackend::OpenGL) ? "OpenGL" :
        (APP::GetBackend() == GBackend::Vulkan) ? "Vulkan" : "Null";
    LOG_STREAM_INFO(kLog) << "backend = " << backendName
        << ", validation = " << (APP::GetEnableValidation() ? "on" : "off");

    WINDOW_KEYWORD::SetWindowSize(1920, 1152);
    WINDOW_KEYWORD::SetIsCursorDisable(false);
    WINDOW_KEYWORD::SetWindowTitle("004_Anti_Aliasing");
    COMPONENT_CONFIG::SetIsEnableGUI(true);

    APP::InitApp();

    {
        CAMERA::FlyCameraConfig cfg{};
        cfg.position  = TitusMath::Vec3{0.0f, 0.0f, 4.0f};
        cfg.yawDeg    = -90.0f;
        cfg.pitchDeg  = 0.0f;
        cfg.fovDeg    = 60.0f;
        cfg.aspect    = 0.0f;
        cfg.nearPlane = 0.1f;
        cfg.farPlane  = 100.0f;
        CAMERA::SetBuiltinFlyCameraConfig(cfg);
        CAMERA::EnableBuiltinFlyCamera(true);
    }

    TitusAsset::ModelAssetData modelAsset{};
    TitusAsset::ModelLoadOptions modelOpts{};
    modelOpts.flipUVs = false; // sponza.obj 的 UV 已是 OpenGL 约定
    const std::string sponzaPath = std::string(SOLUTION_DIR) + "Model/sponza/sponza.obj";
    if (!TitusAsset::LoadModel(sponzaPath, modelAsset, modelOpts))
    {
        LOG_STREAM_ERROR(kLog) << "failed to load Sponza model: " << sponzaPath;
        APP::ShutdownApp();
        return 1;
    }

    TitusMath::Vec3 bbMin(std::numeric_limits<float>::max());
    TitusMath::Vec3 bbMax(-std::numeric_limits<float>::max());
    for (const auto& mesh : modelAsset.meshes)
    {
        bbMin = TitusMath::min(bbMin, mesh.aabbMin);
        bbMax = TitusMath::max(bbMax, mesh.aabbMax);
    }
    if (!(bbMin.x < bbMax.x))
    {
        bbMin = TitusMath::Vec3(-5.0f, -2.0f, -2.0f);
        bbMax = TitusMath::Vec3(5.0f, 3.0f, 2.0f);
    }
    LOG_STREAM_INFO(kLog)
        << "Sponza AABB min=(" << bbMin.x << "," << bbMin.y << "," << bbMin.z << ") "
        << "max=(" << bbMax.x << "," << bbMax.y << "," << bbMax.z << ") "
        << "meshes=" << modelAsset.meshes.size();

    GpuModelHandle sponzaHandle = APP::UploadGpuModel(modelAsset);
    if (!sponzaHandle.IsValid())
    {
        LOG_STREAM_ERROR(kLog) << "UploadGpuModel failed";
        APP::ShutdownApp();
        return 1;
    }

    {
        Sponza sponza(sponzaHandle, TitusMath::Mat4{1.0f});
        TechniqueContext techniqueCtx;
        techniqueCtx.mode = startMode;

        auto scenePass = std::make_shared<ScenePass>();
        auto msaaPass = std::make_shared<MSAAPass>();
        scenePass->SetSponza(&sponza);
        scenePass->SetContext(&techniqueCtx);
        msaaPass->SetSponza(&sponza);
        msaaPass->SetContext(&techniqueCtx);

        auto applySchedule = [&](AATechnique mode)
        {
            if (mode == AATechnique::MSAA)
                APP::SetScheduledPasses({msaaPass});
            else
                APP::SetScheduledPasses({scenePass});
        };

        APP::AddPass(scenePass);
        APP::AddPass(msaaPass);
        applySchedule(techniqueCtx.mode);

        OVERLAY::AddPanel("Anti-Aliasing", [&techniqueCtx, &applySchedule]()
        {
            int m = static_cast<int>(techniqueCtx.mode);
            bool changed = ImGui::RadioButton("None", &m, static_cast<int>(AATechnique::None));
            changed = ImGui::RadioButton("MSAA", &m, static_cast<int>(AATechnique::MSAA)) || changed;
            if (changed)
            {
                techniqueCtx.mode = static_cast<AATechnique>(m);
                applySchedule(techniqueCtx.mode);
            }
            ImGui::TextDisabled("FXAA / SMAA / TAA 尚未接入");

            if (techniqueCtx.mode == AATechnique::MSAA)
            {
                int sampleIdx = (techniqueCtx.msaaSamples == 2) ? 0
                    : (techniqueCtx.msaaSamples == 8) ? 2 : 1;
                const char* sampleItems[] = { "2x", "4x", "8x" };
                if (ImGui::Combo("Samples", &sampleIdx, sampleItems, IM_ARRAYSIZE(sampleItems)))
                    techniqueCtx.msaaSamples = (sampleIdx == 0) ? 2u : (sampleIdx == 2) ? 8u : 4u;
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Main light");
            ImGui::SliderFloat("Yaw", &techniqueCtx.lightYawDeg, -180.0f, 180.0f);
            ImGui::SliderFloat("Pitch", &techniqueCtx.lightPitchDeg, -10.0f, 89.0f);
            ImGui::SliderFloat("Ambient", &techniqueCtx.ambient, 0.0f, 1.0f);
        });

        while (!APP::ShouldClose())
            APP::UpdateApp();

        APP::WaitIdle();
        OVERLAY::ClearPanels();
    }

    APP::ShutdownApp();
    return 0;
}
