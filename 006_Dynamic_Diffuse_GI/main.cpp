// ============================================================================
// 006_Dynamic_Diffuse_GI - main.cpp
//
// Sponza 上的 Dynamic Diffuse GI：GBuffer 光栅化 + Compute rayQuery 更新
// 三维 irradiance probe 场，延迟着色时三线性采样 8 邻域 probe（Chebyshev 可见性）。
// 默认 Vulkan；无 RT Core / 切 OpenGL 时退回直接光。
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

#include "Logger.h"

#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxOverlay.h"
#include "RendererInterface/TitusGfxPass.h"

#include "AssetLoader/AssetTypes.h"
#include "AssetLoader/ModelLoader.h"

#include "DDGIContext.h"
#include "DDGIPass.h"
#include "Sponza.h"
#include "SponzaGBufferPass.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

int main(int argc, char** argv)
{
    using namespace TitusRHI;
    const char* kLog = "006_Dynamic_Diffuse_GI";

    TitusBasic::Logger::Instance().Init(kLog);

    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown)
        APP::SetBackend(GBackend::Vulkan);
    APP::SetThreadingMode(GThreadingMode::Direct);

    bool enableGui = true;
    DDGIViewMode startView = DDGIViewMode::Combined;
    for (int i = 1; i < argc; ++i)
    {
        if (!argv[i])
            continue;
        if (std::strcmp(argv[i], "--no-gui") == 0)
        {
            enableGui = false;
            continue;
        }
        if (std::strncmp(argv[i], "--view=", 7) != 0)
            continue;
        const char* v = argv[i] + 7;
        if (std::strcmp(v, "combined") == 0) startView = DDGIViewMode::Combined;
        else if (std::strcmp(v, "direct") == 0) startView = DDGIViewMode::DirectOnly;
        else if (std::strcmp(v, "gi") == 0) startView = DDGIViewMode::GIOnly;
        else if (std::strcmp(v, "albedo") == 0) startView = DDGIViewMode::Albedo;
        else if (std::strcmp(v, "normal") == 0) startView = DDGIViewMode::Normal;
    }

    const char* backendName =
        (APP::GetBackend() == GBackend::OpenGL) ? "OpenGL" :
        (APP::GetBackend() == GBackend::Vulkan) ? "Vulkan" : "Null";
    LOG_STREAM_INFO(kLog) << "backend = " << backendName
        << ", validation = " << (APP::GetEnableValidation() ? "on" : "off");

    WINDOW_KEYWORD::SetWindowSize(1920, 1080);
    WINDOW_KEYWORD::SetIsCursorDisable(false);
    WINDOW_KEYWORD::SetWindowTitle("006_Dynamic_Diffuse_GI");
    COMPONENT_CONFIG::SetIsEnableGUI(enableGui);

    APP::InitApp();

    {
        CAMERA::FlyCameraConfig cfg{};
        // Sponza 中庭偏 +Z 一侧，略抬头沿 -Z 看向开口。
        cfg.position = TitusMath::Vec3{-0.497625f, -1.88612f, 3.62309f};
        cfg.yawDeg = -78.2999f;
        cfg.pitchDeg = 22.9999f;
        cfg.fovDeg = 60.0f;
        cfg.aspect = 0.0f;
        cfg.nearPlane = 0.1f;
        cfg.farPlane = 80.0f;
        CAMERA::SetBuiltinFlyCameraConfig(cfg);
        CAMERA::EnableBuiltinFlyCamera(true);
    }

    TitusAsset::ModelAssetData modelAsset{};
    TitusAsset::ModelLoadOptions modelOpts{};
    modelOpts.flipUVs = false;
    const std::string sponzaPath = std::string(SOLUTION_DIR) + "Model/sponza/sponza.obj";
    if (!TitusAsset::LoadModel(sponzaPath, modelAsset, modelOpts))
    {
        LOG_STREAM_ERROR(kLog) << "failed to load Sponza: " << sponzaPath;
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

    Sponza sponza(sponzaHandle, TitusMath::Mat4{1.0f});

    DDGIContext ctx;
    ctx.viewMode = startView;
    auto gbufferPass = std::make_shared<SponzaGBufferPass>();
    auto ddgiPass = std::make_shared<DDGIPass>();
    gbufferPass->SetSponza(&sponza);
    ddgiPass->SetContext(&ctx);
    ddgiPass->SetModel(&modelAsset);

    APP::AddPass(gbufferPass);
    APP::AddPass(ddgiPass);

    OVERLAY::AddPanel("DDGI", [&ctx, ddgiPass, kLog]()
    {
        if (!ctx.rayTracingReady)
        {
            ImGui::TextWrapped("当前设备没有 Ray Query。probe 不会更新，画面只有直接光。请用支持 VK_KHR_ray_query 的 GPU，并以 Vulkan 后端启动。");
            ImGui::Separator();
        }
        else
        {
            ImGui::Text("Probe grid %d x %d x %d  (%u probes, 64 rays)",
                        ddgiPass->GetGridX(), ddgiPass->GetGridY(), ddgiPass->GetGridZ(),
                        ddgiPass->GetProbeCount());
        }

        int view = static_cast<int>(ctx.viewMode);
        ImGui::RadioButton("Combined", &view, static_cast<int>(DDGIViewMode::Combined));
        ImGui::SameLine();
        ImGui::RadioButton("Direct", &view, static_cast<int>(DDGIViewMode::DirectOnly));
        ImGui::SameLine();
        ImGui::RadioButton("GI", &view, static_cast<int>(DDGIViewMode::GIOnly));
        ImGui::RadioButton("Albedo", &view, static_cast<int>(DDGIViewMode::Albedo));
        ImGui::SameLine();
        ImGui::RadioButton("Normal", &view, static_cast<int>(DDGIViewMode::Normal));
        ctx.viewMode = static_cast<DDGIViewMode>(view);

        ImGui::Checkbox("Show probes", &ctx.showProbes);
        ImGui::SliderFloat("Probe scale", &ctx.probeVisualScale, 0.02f, 0.18f);
        ImGui::SliderFloat("GI intensity", &ctx.giIntensity, 0.0f, 4.0f);
        ImGui::SliderFloat("Bounce scale", &ctx.bounceScale, 0.0f, 1.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("命中点回采上一帧 probe 场的强度，决定第二次及以后的弹射。\n"
                              "接近 1 时反弹更亮，但收敛更慢。");
        ImGui::SliderFloat("Hysteresis", &ctx.hysteresis, 0.0f, 0.99f);
        ImGui::SliderFloat("Max ray distance", &ctx.maxRayDistance, 4.0f, 40.0f);
        ImGui::SliderFloat("Normal bias", &ctx.normalBias, 0.0f, 0.8f);

        ImGui::Separator();
        ImGui::TextUnformatted("Sun");
        ImGui::SliderFloat3("Direction (照射方向)", &ctx.lightDir.x, -1.0f, 1.0f);
        ImGui::ColorEdit3("Color", &ctx.lightColor.x);
        ImGui::SliderFloat("Intensity", &ctx.lightIntensity, 0.0f, 8.0f);
        ImGui::ColorEdit3("Sky", &ctx.skyColor.x);

        if (ImGui::Button("Reset probes"))
            ctx.resetAccumulation = true;

        ImGui::Separator();
        ImGui::TextUnformatted("Camera");
        const TitusMath::Vec3 camPos = CAMERA::GetMainCameraPosition();
        const TitusMath::Mat4 camView = CAMERA::GetMainCameraViewMatrix();
        // lookAt 第三行是 -forward（列主序：mat[col][row]）
        const TitusMath::Vec3 camFront = TitusMath::normalize(
            TitusMath::Vec3{-camView[0][2], -camView[1][2], -camView[2][2]});
        const float camYawDeg = TitusMath::degrees(std::atan2(camFront.z, camFront.x));
        const float camPitchDeg = TitusMath::degrees(
            std::asin(std::clamp(camFront.y, -1.0f, 1.0f)));
        ImGui::Text("pos   (%.4f, %.4f, %.4f)", camPos.x, camPos.y, camPos.z);
        ImGui::Text("yaw   %.3f   pitch %.3f", camYawDeg, camPitchDeg);
        ImGui::Text("front (%.4f, %.4f, %.4f)", camFront.x, camFront.y, camFront.z);
        if (ImGui::Button("Print camera"))
        {
            LOG_STREAM_INFO(kLog)
                << "camera pos=(" << camPos.x << ", " << camPos.y << ", " << camPos.z << ") "
                << "yaw=" << camYawDeg << " pitch=" << camPitchDeg << " "
                << "front=(" << camFront.x << ", " << camFront.y << ", " << camFront.z << ")";
        }
    });

    while (!APP::ShouldClose())
        APP::UpdateApp();

    APP::WaitIdle();
    APP::ShutdownApp();
    return 0;
}
