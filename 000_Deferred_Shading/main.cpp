// ============================================================================
// 000_Deferred_Shading - main.cpp
//
// 延迟渲染（Deferred Shading）示例：
//   1. SponzaGBufferPass：把 Sponza 渲染进 G-Buffer（Albedo/Normal/Position/Depth）；
//   2. DeferredLightingPass：全屏 Pass 采样 G-Buffer，对 5 个点光源做 Blinn-Phong，
//      结果直接输出到默认 backbuffer。
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

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

// ----------------------------------------------------------------------------
// 基于模型 AABB 自适应生成 5 个点光源：沿包围盒最长的水平轴均匀铺开，放在离地
// 板约 1/3 高度处，半径取包围盒对角线的一部分，配不同色相方便直观区分多光源。
// ----------------------------------------------------------------------------
static std::vector<DeferredLightingPass::PointLightDesc>
MakeLights(const TitusMath::Vec3& bbMin, const TitusMath::Vec3& bbMax)
{
    const TitusMath::Vec3 center = (bbMin + bbMax) * 0.5f;
    const TitusMath::Vec3 size = bbMax - bbMin;
    const float diag = TitusMath::length(size);

    // 选取水平面内较长的轴作为铺开方向（X 或 Z）
    const bool spreadAlongX = size.x >= size.z;
    const float axisMin = spreadAlongX ? bbMin.x : bbMin.z;
    const float axisMax = spreadAlongX ? bbMax.x : bbMax.z;
    const float y = bbMin.y + size.y * 0.35f; // 离地板约 1/3 高度

    // 5 种色相（RGB），强度略有差异，营造彩色多光源效果
    const TitusMath::Vec3 colors[DeferredLightingPass::MAX_LIGHTS] = {
        {1.0f, 0.25f, 0.20f}, // 暖红
        {1.0f, 0.75f, 0.30f}, // 橙黄
        {0.35f, 1.0f, 0.45f}, // 绿
        {0.30f, 0.60f, 1.0f}, // 蓝
        {0.85f, 0.40f, 1.0f}, // 紫
    };

    std::vector<DeferredLightingPass::PointLightDesc> lights;
    lights.reserve(DeferredLightingPass::MAX_LIGHTS);
    for (int i = 0; i < DeferredLightingPass::MAX_LIGHTS; ++i)
    {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(DeferredLightingPass::MAX_LIGHTS);
        const float axisPos = axisMin + (axisMax - axisMin) * t;

        DeferredLightingPass::PointLightDesc l{};
        if (spreadAlongX)
            l.worldPos = TitusMath::Vec3(axisPos, y, center.z);
        else
            l.worldPos = TitusMath::Vec3(center.x, y, axisPos);

        l.radius = diag * 0.45f;
        l.color = colors[i];
        l.intensity = 4.0f;
        lights.push_back(l);
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
    WINDOW_KEYWORD::SetWindowTitle("000_Deferred_Shading");
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

    // 6) 构造并注册 2 个 Pass（GBuffer -> DeferredLighting）
    auto gbufferPass = std::make_shared<SponzaGBufferPass>();
    auto lightingPass = std::make_shared<DeferredLightingPass>();

    gbufferPass->SetSponza(&sponza);
    lightingPass->SetLights(MakeLights(bbMin, bbMax));

    APP::AddPass(gbufferPass);
    APP::AddPass(lightingPass);

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
