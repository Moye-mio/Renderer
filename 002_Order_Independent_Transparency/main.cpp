// ============================================================================
// 002_Order_Independent_Transparency - main.cpp
//
// OIT 半透明算法对比示例（V1 只搭场景，算法未写）：
//   不透明 Cornell Box + 三只同朝向并排的半透明 Stanford Dragon（颜色不同）。
//   ScenePass：先画 Cornell 写深度，再朴素 SrcAlpha 混合画龙——这不是 OIT。
//
// 架构与 000 / 001 一致：仅通过 TitusRHI::* 启动；AssetLoader 解码 CPU IR，
// gfx 上传得到 GpuModelHandle。默认 OpenGL，`--backend=vk` 可切 Vulkan。
// ============================================================================
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Logger.h"

#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxPass.h"
#include "RendererInterface/TitusGfxImGui.h"
#include "RendererInterface/TitusGfxOverlay.h"

#include "AssetLoader/AssetTypes.h"
#include "AssetLoader/ModelLoader.h"

#include "Scene.h"
#include "ScenePass.h"
#include "TechniqueContext.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

namespace
{
    bool LoadGpuModel(const char* logTag,
                      const std::string& path,
                      TitusAsset::ModelAssetData& outAsset,
                      TitusRHI::GpuModelHandle& outHandle)
    {
        TitusAsset::ModelLoadOptions opts{};
        opts.flipUVs = false;
        opts.loadTextures = false; // Cornell / Dragon 都是纯色 MTL，没有贴图
        if (!TitusAsset::LoadModel(path, outAsset, opts))
        {
            LOG_STREAM_ERROR(logTag) << "failed to load model: " << path;
            return false;
        }
        outHandle = TitusRHI::APP::UploadGpuModel(outAsset);
        if (!outHandle.IsValid())
        {
            LOG_STREAM_ERROR(logTag) << "UploadGpuModel failed: " << path;
            return false;
        }
        return true;
    }

    void LogAabb(const char* logTag, const char* name,
                 const TitusMath::Vec3& mn, const TitusMath::Vec3& mx)
    {
        LOG_STREAM_INFO(logTag)
            << name << " AABB min=(" << mn.x << "," << mn.y << "," << mn.z << ") "
            << "max=(" << mx.x << "," << mx.y << "," << mx.z << ")";
    }
}

int main(int argc, char** argv)
{
    using namespace TitusRHI;
    const char* kLog = "002_Order_Independent_Transparency";

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
    WINDOW_KEYWORD::SetWindowTitle("002_Order_Independent_Transparency (Scene Baseline)");
    COMPONENT_CONFIG::SetIsEnableGUI(true);

    APP::InitApp();

    {
        CAMERA::FlyCameraConfig cfg{};
        // 盒子内部 y∈[0,1.99]、开口在 z=+0.99；退到 z=3.4 才能让 50° 竖直 FOV
        // 在开口处覆盖满 1.99 的高度，y 取盒子竖直中心。
        cfg.position  = TitusMath::Vec3{0.0f, 1.00f, 3.40f};
        cfg.yawDeg    = -90.0f; // 朝 -Z，对着 Cornell 后墙
        cfg.pitchDeg  = 0.0f;
        cfg.fovDeg    = 50.0f;
        cfg.aspect    = 0.0f;
        cfg.nearPlane = 0.05f;
        cfg.farPlane  = 20.0f;
        cfg.moveSpeed = 1.5f;
        CAMERA::SetBuiltinFlyCameraConfig(cfg);
        CAMERA::EnableBuiltinFlyCamera(true);
    }

    // McGuire 归档里的空盒子变体（红左墙 / 绿右墙）。同目录下换成
    // CornellBox-Original.obj 就能拿到带 shortBox / tallBox 的经典版，
    // 那两个白盒子会挡住并排的龙，所以这里默认用空盒。
    const std::string cornellPath = std::string(SOLUTION_DIR) + "Model/CornellBox/CornellBox-Empty-RG.obj";
    const std::string dragonPath  = std::string(SOLUTION_DIR) + "Model/Dragon/dragon.obj";

    TitusAsset::ModelAssetData cornellAsset{};
    TitusAsset::ModelAssetData dragonAsset{};
    GpuModelHandle cornellHandle{};
    GpuModelHandle dragonHandle{};
    if (!LoadGpuModel(kLog, cornellPath, cornellAsset, cornellHandle) ||
        !LoadGpuModel(kLog, dragonPath, dragonAsset, dragonHandle))
    {
        if (cornellHandle.IsValid()) APP::DestroyGpuModel(cornellHandle);
        if (dragonHandle.IsValid()) APP::DestroyGpuModel(dragonHandle);
        APP::ShutdownApp();
        return 1;
    }

    TitusMath::Vec3 cornellMin, cornellMax, dragonMin, dragonMax;
    if (!ComputeModelAabb(cornellAsset, cornellMin, cornellMax))
    {
        cornellMin = TitusMath::Vec3(-1.02f, 0.0f, -1.04f);
        cornellMax = TitusMath::Vec3(1.00f, 1.99f, 0.99f);
    }
    if (!ComputeModelAabb(dragonAsset, dragonMin, dragonMax))
    {
        dragonMin = TitusMath::Vec3(-0.5f);
        dragonMax = TitusMath::Vec3(0.5f);
    }
    LogAabb(kLog, "Cornell", cornellMin, cornellMax);
    LogAabb(kLog, "Dragon", dragonMin, dragonMax);

    const TitusMath::Mat4 cornellMatrix{1.0f};
    const auto dragonMatrices = MakeDragonRowTransforms(
        dragonMin, dragonMax, cornellMin, cornellMax, kDragonInstanceCount, 0.55f);
    std::vector<DragonInstance> dragons;
    dragons.reserve(dragonMatrices.size());
    for (int i = 0; i < static_cast<int>(dragonMatrices.size()); ++i)
        dragons.push_back(DragonInstance{dragonMatrices[static_cast<size_t>(i)], DefaultDragonAlbedo(i)});
    std::vector<TitusMath::Vec3> cornellAlbedo = MakeCornellAlbedo(cornellAsset);

    {
        Scene scene(cornellHandle, cornellMatrix, std::move(cornellAlbedo),
                    dragonHandle, std::move(dragons));

        TechniqueContext techniqueCtx;
        auto scenePass = std::make_shared<ScenePass>();
        scenePass->SetScene(&scene);
        scenePass->SetContext(&techniqueCtx);
        APP::AddPass(scenePass);

        OVERLAY::AddPanel("OIT Technique", [&techniqueCtx, &scene]()
        {
            int m = static_cast<int>(techniqueCtx.mode);
            ImGui::RadioButton("Baseline (naive alpha, not OIT)", &m,
                               static_cast<int>(OITTechnique::Baseline));
            techniqueCtx.mode = static_cast<OITTechnique>(m);
            ImGui::Separator();
            ImGui::SliderFloat("Opacity", &techniqueCtx.dragonOpacity, 0.05f, 1.0f);
            auto& dragonsUi = scene.MutableDragons();
            for (size_t i = 0; i < dragonsUi.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                float albedo[3] = {
                    dragonsUi[i].albedo.x,
                    dragonsUi[i].albedo.y,
                    dragonsUi[i].albedo.z
                };
                char label[32];
                std::snprintf(label, sizeof(label), "Dragon %zu", i + 1);
                if (ImGui::ColorEdit3(label, albedo))
                    dragonsUi[i].albedo = TitusMath::Vec3(albedo[0], albedo[1], albedo[2]);
                ImGui::PopID();
            }
            ImGui::TextUnformatted("Weighted Blended / Fourier / Wavelet: not implemented yet.");
        });

        while (!APP::ShouldClose())
            APP::UpdateApp();

        APP::WaitIdle();
        OVERLAY::ClearPanels();
    }

    APP::ShutdownApp();
    return 0;
}
