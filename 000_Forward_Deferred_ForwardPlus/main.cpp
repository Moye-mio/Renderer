// ============================================================================
// 000_Forward_Deferred_ForwardPlus - main.cpp
//
// 延迟 / 前向 / Clustered Forward 着色对比示例：
//   - Deferred：SponzaGBufferPass -> DeferredLightingPass（G-Buffer + 全屏光照）
//   - Forward：ForwardShadingPass（几何片元直接 Blinn-Phong）
//   - Clustered Forward：ForwardPlusPass（Depth 预通道 → Compute 按 cluster 剔灯 → 按 tile+slice 着色）
//   ImGui「Shading Technique」面板切换；SetScheduledPasses 让调度器只挂当前算法的 Pass。
//   三套算法共用 TechniqueContext::shared 的灯与 BRDF。
//
// 架构与 001 一致（后端无关 RHI）：仅通过 RendererInterface 暴露的 `TitusRHI::*`
// API 启动应用、注册 Pass；模型加载走 AssetLoader 解码出 CPU 端 IR，再交给 gfx
// 上传得到 GpuModelHandle。默认 OpenGL 后端，可用 `--backend=vk` 切换。
// Vulkan Validation：`--validation=on|off`（Debug 默认 on，Release 默认 off）。
// ============================================================================
#include <algorithm>
#include <cmath>
#include <limits>
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

#include "Sponza.h"
#include "SponzaGBufferPass.h"
#include "DeferredLightingPass.h"
#include "ForwardShadingPass.h"
#include "ForwardPlusPass.h"
#include "TechniqueContext.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

// ----------------------------------------------------------------------------
// 基于模型 AABB 自适应生成 count 个点光源：在包围盒内按近似立方网格铺开，
// 半径取网格单元对角线的一部分，5 种色相循环，避免挤在一条线上过曝。
// count=1000 时退化为原来的 10x10x10。
// ----------------------------------------------------------------------------
static std::vector<PointLightDesc>
MakeLights(const TitusMath::Vec3& bbMin, const TitusMath::Vec3& bbMax, int count)
{
    count = std::clamp(count, 10, SharedShadingParams::MAX_LIGHTS);

    // 近似立方体素：gz ≈ ∛n，再把剩余二维拆成 gy × gx，保证 gx*gy*gz >= count。
    const int gridZ = std::max(1, static_cast<int>(std::round(std::cbrt(static_cast<double>(count)))));
    const int remain = (count + gridZ - 1) / gridZ;
    const int gridY = std::max(1, static_cast<int>(std::round(std::sqrt(static_cast<double>(remain)))));
    const int gridX = std::max(1, (count + gridY * gridZ - 1) / (gridY * gridZ));

    const TitusMath::Vec3 size = bbMax - bbMin;
    const TitusMath::Vec3 colors[] = {
        {1.0f, 0.25f, 0.20f}, // 暖红
        {1.0f, 0.75f, 0.30f}, // 橙黄
        {0.35f, 1.0f, 0.45f}, // 绿
        {0.30f, 0.60f, 1.0f}, // 蓝
        {0.85f, 0.40f, 1.0f}, // 紫
    };
    constexpr int kColorCount = static_cast<int>(sizeof(colors) / sizeof(colors[0]));

    const float pad = 0.08f;
    const TitusMath::Vec3 innerMin = bbMin + size * pad;
    const TitusMath::Vec3 innerSize = size * (1.0f - 2.0f * pad);
    const TitusMath::Vec3 cell(
        innerSize.x / static_cast<float>(gridX),
        innerSize.y / static_cast<float>(gridY),
        innerSize.z / static_cast<float>(gridZ));
    const float radius = TitusMath::length(cell) * 1.25f;

    std::vector<PointLightDesc> lights;
    lights.reserve(static_cast<size_t>(count));
    int i = 0;
    for (int iz = 0; iz < gridZ && i < count; ++iz)
    {
        for (int iy = 0; iy < gridY && i < count; ++iy)
        {
            for (int ix = 0; ix < gridX && i < count; ++ix, ++i)
            {
                PointLightDesc l{};
                l.worldPos = TitusMath::Vec3(
                    innerMin.x + (static_cast<float>(ix) + 0.5f) * cell.x,
                    innerMin.y + (static_cast<float>(iy) + 0.5f) * cell.y,
                    innerMin.z + (static_cast<float>(iz) + 0.5f) * cell.z);
                l.radius = radius;
                l.color = colors[i % kColorCount];
                l.intensity = 2.5f;
                lights.push_back(l);
            }
        }
    }
    return lights;
}

int main(int argc, char** argv)
{
    using namespace TitusRHI;

    // 0) Logger
    TitusBasic::Logger::Instance().Init("000_Forward_Deferred_ForwardPlus");

    // 1) 解析 --backend=gl|vk|null、--validation=on|off，默认 OpenGL
    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown)
    {
        APP::SetBackend(GBackend::OpenGL);
    }
    const char* backendName =
        (APP::GetBackend() == GBackend::OpenGL) ? "OpenGL" :
        (APP::GetBackend() == GBackend::Vulkan) ? "Vulkan" : "Null";
    LOG_STREAM_INFO("000_Forward_Deferred_ForwardPlus") << "backend = " << backendName
        << ", validation = " << (APP::GetEnableValidation() ? "on" : "off");

    // 2) 窗口 / 组件配置
    WINDOW_KEYWORD::SetWindowSize(1920, 1152);
    WINDOW_KEYWORD::SetIsCursorDisable(false);
    WINDOW_KEYWORD::SetWindowTitle("000_Forward_Deferred_ForwardPlus (Technique Compare)");
    COMPONENT_CONFIG::SetIsEnableGUI(true);

    // 3) 初始化（窗口 + 设备 + PassScheduler）
    APP::InitApp();

    // 4) 内置 FPS 飞行相机
    {
        CAMERA::FlyCameraConfig cfg{};
        cfg.position  = TitusMath::Vec3{0.0f, 0.0f, 4.0f};
        cfg.yawDeg    = -90.0f;
        cfg.pitchDeg  = 0.0f;
        cfg.fovDeg    = 60.0f;
        cfg.aspect    = 0.0f;   // 自动跟随窗口
        cfg.nearPlane = 0.1f;
        cfg.farPlane  = 100.0f;
        CAMERA::SetBuiltinFlyCameraConfig(cfg);
        CAMERA::EnableBuiltinFlyCamera(true);
    }

    // 5) AssetLoader 解码 -> UploadGpuModel -> 构造 Sponza
    TitusAsset::ModelAssetData modelAsset{};
    TitusAsset::ModelLoadOptions modelOpts{};
    modelOpts.flipUVs = false; // sponza.obj 的 UV 已是 OpenGL 约定
    const std::string sponzaPath = std::string(SOLUTION_DIR) + "Model/sponza/sponza.obj";
    if (!TitusAsset::LoadModel(sponzaPath, modelAsset, modelOpts))
    {
        LOG_STREAM_ERROR("000_Forward_Deferred_ForwardPlus") << "failed to load Sponza model: " << sponzaPath;
        APP::ShutdownApp();
        return 1;
    }

    // 5.1) 统计模型 AABB（用于自适应布置光源）
    TitusMath::Vec3 bbMin(std::numeric_limits<float>::max());
    TitusMath::Vec3 bbMax(-std::numeric_limits<float>::max());
    for (const auto& mesh : modelAsset.meshes)
    {
        bbMin = TitusMath::min(bbMin, mesh.aabbMin);
        bbMax = TitusMath::max(bbMax, mesh.aabbMax);
    }
    // 兜底：AABB 无效时给一个合理的默认盒
    if (!(bbMin.x < bbMax.x))
    {
        bbMin = TitusMath::Vec3(-5.0f, -2.0f, -2.0f);
        bbMax = TitusMath::Vec3(5.0f, 3.0f, 2.0f);
    }
    LOG_STREAM_INFO("000_Forward_Deferred_ForwardPlus")
        << "Sponza AABB min=(" << bbMin.x << "," << bbMin.y << "," << bbMin.z << ") "
        << "max=(" << bbMax.x << "," << bbMax.y << "," << bbMax.z << ")";

    GpuModelHandle sponzaHandle = APP::UploadGpuModel(modelAsset);
    if (!sponzaHandle.IsValid())
    {
        LOG_STREAM_ERROR("000_Forward_Deferred_ForwardPlus") << "UploadGpuModel failed.";
        APP::ShutdownApp();
        return 1;
    }

    TitusMath::Mat4 sponzaModelMatrix{1.0f};
    Sponza sponza(sponzaHandle, sponzaModelMatrix);

    // 6) TechniqueContext + 全部 Pass：AddPass（Init 一次），调度列表按 mode 互斥。
    TechniqueContext techniqueCtx;
    techniqueCtx.shared.lights = MakeLights(bbMin, bbMax, SharedShadingParams::MAX_LIGHTS);

    auto gbufferPass = std::make_shared<SponzaGBufferPass>();
    auto lightingPass = std::make_shared<DeferredLightingPass>();
    auto forwardPass = std::make_shared<ForwardShadingPass>();
    auto forwardPlusPass = std::make_shared<ForwardPlusPass>();

    gbufferPass->SetSponza(&sponza);
    gbufferPass->SetContext(&techniqueCtx);
    lightingPass->SetContext(&techniqueCtx);
    forwardPass->SetSponza(&sponza);
    forwardPass->SetContext(&techniqueCtx);
    forwardPlusPass->SetSponza(&sponza);
    forwardPlusPass->SetContext(&techniqueCtx);

    auto applySchedule = [&](ShadingTechnique mode)
    {
        if (mode == ShadingTechnique::Deferred)
            APP::SetScheduledPasses({gbufferPass, lightingPass});
        else if (mode == ShadingTechnique::ForwardPlus)
            APP::SetScheduledPasses({forwardPlusPass});
        else
            APP::SetScheduledPasses({forwardPass});
    };

    // 先登记并 Init 全部，再把调度列表收成当前 mode（默认 Deferred）。
    APP::AddPass(gbufferPass);
    APP::AddPass(lightingPass);
    APP::AddPass(forwardPass);
    APP::AddPass(forwardPlusPass);
    applySchedule(techniqueCtx.mode);

    OVERLAY::AddPanel("Shading Technique", [&techniqueCtx, &applySchedule, bbMin, bbMax]()
    {
        int m = static_cast<int>(techniqueCtx.mode);
        bool changed = ImGui::RadioButton("Deferred", &m, static_cast<int>(ShadingTechnique::Deferred));
        ImGui::SameLine();
        changed = ImGui::RadioButton("Forward", &m, static_cast<int>(ShadingTechnique::Forward)) || changed;
        ImGui::SameLine();
        changed = ImGui::RadioButton("Clustered Forward", &m, static_cast<int>(ShadingTechnique::ForwardPlus)) || changed;
        if (changed)
        {
            techniqueCtx.mode = static_cast<ShadingTechnique>(m);
            applySchedule(techniqueCtx.mode);
        }

        int lightCount = static_cast<int>(techniqueCtx.shared.lights.size());
        if (ImGui::SliderInt("Lights", &lightCount, 10, SharedShadingParams::MAX_LIGHTS))
            techniqueCtx.shared.lights = MakeLights(bbMin, bbMax, lightCount);

        if (techniqueCtx.mode == ShadingTechnique::Deferred)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Deferred");
            int dv = static_cast<int>(techniqueCtx.deferred.debugView);
            const char* items[] = { "Final", "Albedo", "Normal", "Position" };
            ImGui::Combo("Debug view", &dv, items, IM_ARRAYSIZE(items));
            techniqueCtx.deferred.debugView = static_cast<DeferredParams::DebugView>(dv);
        }
        else if (techniqueCtx.mode == ShadingTechnique::ForwardPlus)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Clustered Forward");
            int dv = static_cast<int>(techniqueCtx.forwardPlus.debugView);
            const char* items[] = { "Final", "Cluster heatmap", "Slice index" };
            ImGui::Combo("Debug view", &dv, items, IM_ARRAYSIZE(items));
            techniqueCtx.forwardPlus.debugView = static_cast<ForwardPlusParams::DebugView>(dv);
        }
    });

    // 7) 主循环
    while (!APP::ShouldClose())
    {
        APP::UpdateApp();
    }

    // 8) 退出
    APP::WaitIdle();
    APP::ShutdownApp();
    return 0;
}
