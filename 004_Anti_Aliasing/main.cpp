// ============================================================================
// 004_Anti_Aliasing - main.cpp
//
// 抗锯齿对比示例：Sponza 上切换 None / MSAA / FXAA / TAA / FSR1.0 / FSR2.0。
// SMAA 后续按 TechniqueContext::mode 接入。
//
// 架构与 000 / 001 / 003 一致：仅通过 TitusRHI::* 启动；AssetLoader 解码
// CPU IR，gfx 上传得到 GpuModelHandle。默认 OpenGL，`--backend=vk` 可切 Vulkan。
// ============================================================================
#include <cmath>
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

#include "FSR2Pass.h"
#include "FSRPass.h"
#include "FXAAPass.h"
#include "MSAAPass.h"
#include "ScenePass.h"
#include "Sponza.h"
#include "TAAPass.h"
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
        else if (std::strcmp(v, "fxaa") == 0 || std::strcmp(v, "FXAA") == 0)
            startMode = AATechnique::FXAA;
        else if (std::strcmp(v, "taa") == 0 || std::strcmp(v, "TAA") == 0)
            startMode = AATechnique::TAA;
        else if (std::strcmp(v, "fsr") == 0 || std::strcmp(v, "FSR") == 0
            || std::strcmp(v, "fsr1.0") == 0 || std::strcmp(v, "FSR1.0") == 0)
            startMode = AATechnique::FSR;
        else if (std::strcmp(v, "fsr2") == 0 || std::strcmp(v, "FSR2") == 0
            || std::strcmp(v, "fsr2.0") == 0 || std::strcmp(v, "FSR2.0") == 0)
            startMode = AATechnique::FSR2;
        else
            LOG_STREAM_ERROR(kLog) << "Unknown --mode value: " << v
                << " (expected none|msaa|fxaa|taa|fsr|fsr2)";
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
        auto fxaaPass = std::make_shared<FXAAPass>();
        auto taaPass = std::make_shared<TAAPass>();
        auto fsrPass = std::make_shared<FSRPass>();
        auto fsr2Pass = std::make_shared<FSR2Pass>();
        scenePass->SetSponza(&sponza);
        scenePass->SetContext(&techniqueCtx);
        msaaPass->SetSponza(&sponza);
        msaaPass->SetContext(&techniqueCtx);
        fxaaPass->SetSponza(&sponza);
        fxaaPass->SetContext(&techniqueCtx);
        taaPass->SetSponza(&sponza);
        taaPass->SetContext(&techniqueCtx);
        fsrPass->SetSponza(&sponza);
        fsrPass->SetContext(&techniqueCtx);
        fsr2Pass->SetSponza(&sponza);
        fsr2Pass->SetContext(&techniqueCtx);

        auto applySchedule = [&](AATechnique mode)
        {
            if (mode == AATechnique::MSAA)
                APP::SetScheduledPasses({msaaPass});
            else if (mode == AATechnique::FXAA)
                APP::SetScheduledPasses({fxaaPass});
            else if (mode == AATechnique::TAA)
                APP::SetScheduledPasses({taaPass});
            else if (mode == AATechnique::FSR)
                APP::SetScheduledPasses({fsrPass});
            else if (mode == AATechnique::FSR2)
                APP::SetScheduledPasses({fsr2Pass});
            else
                APP::SetScheduledPasses({scenePass});
        };

        APP::AddPass(scenePass);
        APP::AddPass(msaaPass);
        APP::AddPass(fxaaPass);
        APP::AddPass(taaPass);
        APP::AddPass(fsrPass);
        APP::AddPass(fsr2Pass);
        applySchedule(techniqueCtx.mode);

        OVERLAY::AddPanel("Anti-Aliasing", [&techniqueCtx, &applySchedule]()
        {
            int m = static_cast<int>(techniqueCtx.mode);
            bool changed = ImGui::RadioButton("None", &m, static_cast<int>(AATechnique::None));
            changed = ImGui::RadioButton("MSAA", &m, static_cast<int>(AATechnique::MSAA)) || changed;
            changed = ImGui::RadioButton("FXAA", &m, static_cast<int>(AATechnique::FXAA)) || changed;
            changed = ImGui::RadioButton("TAA", &m, static_cast<int>(AATechnique::TAA)) || changed;
            changed = ImGui::RadioButton("FSR1.0", &m, static_cast<int>(AATechnique::FSR)) || changed;
            changed = ImGui::RadioButton("FSR2.0", &m, static_cast<int>(AATechnique::FSR2)) || changed;
            if (changed)
            {
                techniqueCtx.mode = static_cast<AATechnique>(m);
                if (techniqueCtx.mode == AATechnique::TAA)
                    techniqueCtx.taaResetHistory = true;
                if (techniqueCtx.mode == AATechnique::FSR2)
                    techniqueCtx.fsr2ResetHistory = true;
                applySchedule(techniqueCtx.mode);
            }
            ImGui::TextDisabled("SMAA 尚未接入");

            if (techniqueCtx.mode == AATechnique::MSAA)
            {
                int sampleIdx = (techniqueCtx.msaaSamples == 2) ? 0
                    : (techniqueCtx.msaaSamples == 8) ? 2 : 1;
                const char* sampleItems[] = { "2x", "4x", "8x" };
                if (ImGui::Combo("Samples", &sampleIdx, sampleItems, IM_ARRAYSIZE(sampleItems)))
                    techniqueCtx.msaaSamples = (sampleIdx == 0) ? 2u : (sampleIdx == 2) ? 8u : 4u;
            }

            if (techniqueCtx.mode == AATechnique::FXAA)
            {
                ImGui::SliderFloat("Subpix", &techniqueCtx.fxaaSubpix, 0.0f, 1.0f);
                ImGui::SliderFloat("Edge Threshold", &techniqueCtx.fxaaEdgeThreshold, 0.0312f, 0.333f);
                ImGui::SliderFloat("Edge Threshold Min", &techniqueCtx.fxaaEdgeThresholdMin, 0.01f, 0.1f);
            }

            if (techniqueCtx.mode == AATechnique::TAA)
            {
                ImGui::SliderFloat("Feedback", &techniqueCtx.taaFeedback, 0.01f, 0.5f);
                const char* clampItems[] = { "Off", "AABB", "Variance" };
                ImGui::Combo("Clamp", &techniqueCtx.taaClampMode, clampItems, IM_ARRAYSIZE(clampItems));
                ImGui::SliderFloat("Jitter Scale", &techniqueCtx.taaJitterScale, 0.0f, 2.0f);
                if (ImGui::Button("Reset History"))
                    techniqueCtx.taaResetHistory = true;
            }

            if (techniqueCtx.mode == AATechnique::FSR)
            {
                ImGui::SliderFloat("Render Scale", &techniqueCtx.fsrRenderScale, 0.5f, 1.0f);
                const char* upscaleItems[] = { "Bilinear", "EASU", "Mobile EASU" };
                ImGui::Combo("Upscale", &techniqueCtx.fsrUpscaleMode,
                    upscaleItems, IM_ARRAYSIZE(upscaleItems));
                ImGui::Checkbox("RCAS", &techniqueCtx.fsrEnableRcas);
                if (techniqueCtx.fsrEnableRcas)
                    ImGui::SliderFloat("Sharpness (stops)", &techniqueCtx.fsrSharpnessStops, 0.0f, 2.0f);
            }

            if (techniqueCtx.mode == AATechnique::FSR2)
            {
                int quality = 4;
                if (std::abs(techniqueCtx.fsr2RenderScale - (1.0f / 1.5f)) < 0.005f)
                    quality = 0;
                else if (std::abs(techniqueCtx.fsr2RenderScale - (1.0f / 1.7f)) < 0.005f)
                    quality = 1;
                else if (std::abs(techniqueCtx.fsr2RenderScale - 0.5f) < 0.005f)
                    quality = 2;
                else if (std::abs(techniqueCtx.fsr2RenderScale - (1.0f / 3.0f)) < 0.005f)
                    quality = 3;
                const char* qualityItems[] = {
                    "Quality 1.5x", "Balanced 1.7x", "Performance 2.0x", "Ultra 3.0x", "Custom"
                };
                if (ImGui::Combo("Quality", &quality, qualityItems, IM_ARRAYSIZE(qualityItems))
                    && quality < 4)
                {
                    const float scales[] = { 1.0f / 1.5f, 1.0f / 1.7f, 0.5f, 1.0f / 3.0f };
                    techniqueCtx.fsr2RenderScale = scales[quality];
                    techniqueCtx.fsr2ResetHistory = true;
                }
                ImGui::SliderFloat("Render Scale", &techniqueCtx.fsr2RenderScale, 0.33f, 1.0f);
                ImGui::SliderFloat("Feedback", &techniqueCtx.fsr2Feedback, 0.01f, 0.5f);
                const char* clampItems[] = { "Off", "AABB", "Variance" };
                ImGui::Combo("Clamp", &techniqueCtx.fsr2ClampMode, clampItems, IM_ARRAYSIZE(clampItems));
                ImGui::SliderFloat("Jitter Scale", &techniqueCtx.fsr2JitterScale, 0.0f, 2.0f);
                ImGui::Checkbox("RCAS", &techniqueCtx.fsr2EnableRcas);
                if (techniqueCtx.fsr2EnableRcas)
                    ImGui::SliderFloat("Sharpness (stops)", &techniqueCtx.fsr2SharpnessStops, 0.0f, 2.0f);
                if (ImGui::Button("Reset History"))
                    techniqueCtx.fsr2ResetHistory = true;
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
