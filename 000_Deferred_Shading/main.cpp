// ============================================================================
// 000_Deferred_Shading - main.cpp
//
// 延迟 / 前向着色对比示例：
//   - Deferred：SponzaGBufferPass -> DeferredLightingPass（G-Buffer + 全屏光照）
//   - Forward：ForwardShadingPass（几何片元直接 Blinn-Phong）
//   ImGui「Shading Technique」面板切换；SetScheduledPasses 让调度器只挂当前算法的 Pass。
//   两边共用 TechniqueContext::shared 的灯与 BRDF。
//
// 架构与 001 一致（后端无关 RHI）：仅通过 RendererInterface 暴露的 `TitusRHI::*`
// API 启动应用、注册 Pass；模型加载走 AssetLoader 解码出 CPU 端 IR，再交给 gfx
// 上传得到 GpuModelHandle。默认 OpenGL 后端，可用 `--backend=vk` 切换。
// Vulkan Validation：`--validation=on|off`（Debug 默认 on，Release 默认 off）。
// ============================================================================
#include <algorithm>
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
#include "TechniqueContext.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

// ----------------------------------------------------------------------------
// 基于模型 AABB 自适应生成 1000 个点光源：在包围盒内按 10x10x10 网格铺开，
// 半径取网格单元对角线的一部分，5 种色相循环，避免挤在一条线上过曝。
// ----------------------------------------------------------------------------
static std::vector<PointLightDesc>
MakeLights(const TitusMath::Vec3& bbMin, const TitusMath::Vec3& bbMax)
{
    const TitusMath::Vec3 size = bbMax - bbMin;
    constexpr int kGridX = 10;
    constexpr int kGridY = 10;
    constexpr int kGridZ = 10;
    static_assert(kGridX * kGridY * kGridZ == SharedShadingParams::MAX_LIGHTS,
                  "light grid must match MAX_LIGHTS");

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
        innerSize.x / static_cast<float>(kGridX),
        innerSize.y / static_cast<float>(kGridY),
        innerSize.z / static_cast<float>(kGridZ));
    const float radius = TitusMath::length(cell) * 1.25f;

    std::vector<PointLightDesc> lights;
    lights.reserve(SharedShadingParams::MAX_LIGHTS);
    int i = 0;
    for (int iz = 0; iz < kGridZ; ++iz)
    {
        for (int iy = 0; iy < kGridY; ++iy)
        {
            for (int ix = 0; ix < kGridX; ++ix, ++i)
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
    TitusBasic::Logger::Instance().Init("000_Deferred_Shading");

    // 1) 解析 --backend=gl|vk|null、--validation=on|off，默认 OpenGL
    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown)
    {
        APP::SetBackend(GBackend::OpenGL);
    }
    const char* backendName =
        (APP::GetBackend() == GBackend::OpenGL) ? "OpenGL" :
        (APP::GetBackend() == GBackend::Vulkan) ? "Vulkan" : "Null";
    LOG_STREAM_INFO("000_Deferred_Shading") << "backend = " << backendName
        << ", validation = " << (APP::GetEnableValidation() ? "on" : "off");

    // 2) 窗口 / 组件配置
    WINDOW_KEYWORD::SetWindowSize(1920, 1152);
    WINDOW_KEYWORD::SetIsCursorDisable(false);
    WINDOW_KEYWORD::SetWindowTitle("000_Deferred_Shading (Technique Compare)");
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
        LOG_STREAM_ERROR("000_Deferred_Shading") << "failed to load Sponza model: " << sponzaPath;
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
    LOG_STREAM_INFO("000_Deferred_Shading")
        << "Sponza AABB min=(" << bbMin.x << "," << bbMin.y << "," << bbMin.z << ") "
        << "max=(" << bbMax.x << "," << bbMax.y << "," << bbMax.z << ")";

    GpuModelHandle sponzaHandle = APP::UploadGpuModel(modelAsset);
    if (!sponzaHandle.IsValid())
    {
        LOG_STREAM_ERROR("000_Deferred_Shading") << "UploadGpuModel failed.";
        APP::ShutdownApp();
        return 1;
    }

    TitusMath::Mat4 sponzaModelMatrix{1.0f};
    Sponza sponza(sponzaHandle, sponzaModelMatrix);

    // 6) TechniqueContext + 三个 Pass：全部 AddPass（Init 一次），调度列表按 mode 互斥。
    TechniqueContext techniqueCtx;
    techniqueCtx.shared.lights = MakeLights(bbMin, bbMax);

    auto gbufferPass = std::make_shared<SponzaGBufferPass>();
    auto lightingPass = std::make_shared<DeferredLightingPass>();
    auto forwardPass = std::make_shared<ForwardShadingPass>();

    gbufferPass->SetSponza(&sponza);
    gbufferPass->SetContext(&techniqueCtx);
    lightingPass->SetContext(&techniqueCtx);
    forwardPass->SetSponza(&sponza);
    forwardPass->SetContext(&techniqueCtx);

    auto applySchedule = [&](ShadingTechnique mode)
    {
        if (mode == ShadingTechnique::Deferred)
            APP::SetScheduledPasses({gbufferPass, lightingPass});
        else
            APP::SetScheduledPasses({forwardPass});
    };

    // 先登记并 Init 全部，再把调度列表收成当前 mode（默认 Deferred）。
    APP::AddPass(gbufferPass);
    APP::AddPass(lightingPass);
    APP::AddPass(forwardPass);
    applySchedule(techniqueCtx.mode);

    OVERLAY::AddPanel("Shading Technique", [&techniqueCtx, &applySchedule]()
    {
        int m = static_cast<int>(techniqueCtx.mode);
        bool changed = ImGui::RadioButton("Deferred", &m, static_cast<int>(ShadingTechnique::Deferred));
        ImGui::SameLine();
        changed = ImGui::RadioButton("Forward", &m, static_cast<int>(ShadingTechnique::Forward)) || changed;
        if (changed)
        {
            techniqueCtx.mode = static_cast<ShadingTechnique>(m);
            applySchedule(techniqueCtx.mode);
        }

        ImGui::Text("Lights: %d", static_cast<int>(techniqueCtx.shared.lights.size()));

        if (techniqueCtx.mode == ShadingTechnique::Deferred)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Deferred");
            int dv = static_cast<int>(techniqueCtx.deferred.debugView);
            const char* items[] = { "Final", "Albedo", "Normal", "Position" };
            ImGui::Combo("Debug view", &dv, items, IM_ARRAYSIZE(items));
            techniqueCtx.deferred.debugView = static_cast<DeferredParams::DebugView>(dv);
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
