// ============================================================================
// 002_Order_Independent_Transparency - main.cpp
//
// OIT 半透明算法对比示例：
//   不透明 Cornell Box + 三只呈风车状互相穿插咬合的半透明 Stanford Dragon。
//   Baseline：ScenePass 朴素 SrcAlpha 混合（不是 OIT）。
//   Weighted Blended：Accum / Revealage / Blend。
//
// 场景为什么这么摆：排序错误的可见程度是 a_f * a_b * (c_f - c_b)，即取决于两层
// 的 alpha 乘积与颜色差。三只龙风车状咬合同时拉满了颜色差（青/品红/金互补）与
// 重叠面积，而互相穿插还让 per-object 排序在原理上就没有正确解 —— 配合 UI 上的
// 顺序开关，演示链条是：原序（明显错）→ 按视距排序（好转但咬合处仍错）→ WBOIT。
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
#include "WeightedBlendedOITPass.h"

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
    WINDOW_KEYWORD::SetWindowTitle("002_Order_Independent_Transparency");
    COMPONENT_CONFIG::SetIsEnableGUI(true);

    APP::InitApp();

    {
        CAMERA::FlyCameraConfig cfg{};
        // 盒子内部 y∈[0,1.99]、开口在 z=+0.99；退到 z=3.4 才能让 50° 竖直 FOV
        // 在开口处覆盖满 1.99 的高度。风车是在水平面上展开的，纯平视会把三只龙
        // 压成一条线，要俯视到 25° 以上才看得出"三龙相衔"的环状结构。
        cfg.position  = TitusMath::Vec3{0.0f, 2.05f, 3.05f};
        cfg.yawDeg    = -90.0f; // 朝 -Z，对着 Cornell 后墙
        cfg.pitchDeg  = -26.0f;
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
    // 矩阵与 worldCenter 统一交给 Scene::ApplyDragonLayout 生成，这里只定颜色。
    std::vector<DragonInstance> dragons;
    dragons.reserve(static_cast<size_t>(kDragonInstanceCount));
    for (int i = 0; i < kDragonInstanceCount; ++i)
    {
        DragonInstance inst{};
        inst.albedo = DefaultDragonAlbedo(i);
        dragons.push_back(inst);
    }
    std::vector<TitusMath::Vec3> cornellAlbedo = MakeCornellAlbedo(cornellAsset);

    {
        Scene scene(cornellHandle, cornellMatrix, std::move(cornellAlbedo),
                    dragonHandle, std::move(dragons));
        scene.SetLayoutBounds(dragonMin, dragonMax, cornellMin, cornellMax);

        DragonLayoutParams layoutParams{};
        scene.ApplyDragonLayout(layoutParams);

        TechniqueContext techniqueCtx;
        auto scenePass = std::make_shared<ScenePass>();
        auto wboitPass = std::make_shared<WeightedBlendedOITPass>();
        scenePass->SetScene(&scene);
        scenePass->SetContext(&techniqueCtx);
        wboitPass->SetScene(&scene);
        wboitPass->SetContext(&techniqueCtx);

        auto applySchedule = [&](OITTechnique mode)
        {
            if (mode == OITTechnique::WeightedBlended)
                APP::SetScheduledPasses({wboitPass});
            else
                APP::SetScheduledPasses({scenePass});
        };

        APP::AddPass(scenePass);
        APP::AddPass(wboitPass);
        applySchedule(techniqueCtx.mode);

        OVERLAY::AddPanel("OIT Technique", [&techniqueCtx, &scene, &applySchedule, &layoutParams]()
        {
            int m = static_cast<int>(techniqueCtx.mode);
            bool changed = ImGui::RadioButton("Baseline (naive alpha)", &m,
                                              static_cast<int>(OITTechnique::Baseline));
            changed = ImGui::RadioButton("Weighted Blended OIT", &m,
                                         static_cast<int>(OITTechnique::WeightedBlended)) || changed;
            if (changed)
            {
                techniqueCtx.mode = static_cast<OITTechnique>(m);
                applySchedule(techniqueCtx.mode);
            }

            // 核心对照开关：Baseline 下三档结果各不相同，WBOIT 下三档完全一致。
            ImGui::Separator();
            ImGui::TextUnformatted("Draw order");
            int o = static_cast<int>(techniqueCtx.drawOrder);
            bool orderChanged = ImGui::RadioButton("Unsorted", &o,
                                                   static_cast<int>(DragonDrawOrder::SceneOrder));
            orderChanged = ImGui::RadioButton("Back to front", &o,
                                              static_cast<int>(DragonDrawOrder::BackToFront)) || orderChanged;
            orderChanged = ImGui::RadioButton("Front to back", &o,
                                              static_cast<int>(DragonDrawOrder::FrontToBack)) || orderChanged;
            if (orderChanged)
                techniqueCtx.drawOrder = static_cast<DragonDrawOrder>(o);

            ImGui::Separator();
            ImGui::TextUnformatted("Layout");
            int l = static_cast<int>(layoutParams.layout);
            bool layoutChanged = ImGui::RadioButton("Pinwheel", &l,
                                                    static_cast<int>(DragonLayout::Pinwheel));
            layoutChanged = ImGui::RadioButton("Row", &l,
                                               static_cast<int>(DragonLayout::Row)) || layoutChanged;
            if (layoutChanged)
                layoutParams.layout = static_cast<DragonLayout>(l);
            if (layoutParams.layout == DragonLayout::Pinwheel)
            {
                layoutChanged = ImGui::SliderFloat("Interlock", &layoutParams.interlock, 0.0f, 1.0f)
                    || layoutChanged;
                layoutChanged = ImGui::SliderFloat("Blade", &layoutParams.bladeDeg, 0.0f, 180.0f)
                    || layoutChanged;
                layoutChanged = ImGui::SliderFloat("Phase", &layoutParams.phaseDeg, 0.0f, 360.0f)
                    || layoutChanged;
            }
            layoutChanged = ImGui::SliderFloat("Size", &layoutParams.heightFill, 0.15f, 0.95f)
                || layoutChanged;
            if (layoutChanged)
                scene.ApplyDragonLayout(layoutParams);

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
            if (techniqueCtx.mode == OITTechnique::WeightedBlended)
            {
                ImGui::Separator();
                ImGui::TextUnformatted("WBOIT weight (view-space z)");
                ImGui::SliderFloat("Weighted1", &techniqueCtx.weighted1, 0.1f, 20.0f);
                ImGui::SliderFloat("Weighted2", &techniqueCtx.weighted2, 1.0f, 80.0f);
                ImGui::SliderFloat("Weighted1Exp", &techniqueCtx.weighted1Exp, 0.5f, 6.0f);
                ImGui::SliderFloat("Weighted2Exp", &techniqueCtx.weighted2Exp, 0.5f, 8.0f);
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
