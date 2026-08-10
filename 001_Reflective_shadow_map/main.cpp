// ============================================================================
// 001_Reflective_shadow_map - main.cpp
//
// 路径①：业务工程已完全脱离旧 Renderer/。仅通过 RendererInterface 暴露的
// `TitusRHI::*` API 启动应用、注册 Pass；模型加载走 AssetLoader 解码出
// CPU 端 IR（`TitusAsset::ModelAssetData`），再交给 `gfx` 上传得到
// `GpuModelHandle`，最后由 Sponza 持有 handle 给 4 个 Pass 共用。
//
// GL + VK 双后端：
//   - 默认 OpenGL 后端；
//   - `--backend=vk` 走 Vulkan 全栈（Compute Pipeline + DescriptorPool + glslang
//     在线编译）。
// ============================================================================
#include <iostream>
#include <memory>

#include "Logger.h"

#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxPass.h"
#include "RendererInterface/TitusGfxImGui.h"
#include "RendererInterface/TitusGfxOverlay.h"

// 业务侧资产加载入口（CPU IR）
#include "AssetLoader/AssetTypes.h"
#include "AssetLoader/ModelLoader.h"

#include "Sponza.h"
#include "SponzaGBufferPass.h"
#include "RSMBufferPass.h"
#include "ShadingWithRSMPass.h"
#include "ScreenQuadPass.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

// ----------------------------------------------------------------------------
// FPS 飞行相机已下沉到 RendererInterface（CAMERA::EnableBuiltinFlyCamera）。
//   - WASD：前后左右平移；Q/E：上下平移；LSHIFT 加速、LCTRL 减速
//   - 鼠标右键拖拽：旋转视角（yaw/pitch）
// 业务侧只需配置初始位姿/速度，主循环由 APP::UpdateApp 自动驱动。
// ----------------------------------------------------------------------------

int main(int argc, char** argv)
{
    using namespace TitusRHI;

    // 0) 初始化 Logger（尽早，使后续初始化失败也能被记录到文件）
    TitusBasic::Logger::Instance().Init("001_Reflective_shadow_map");

    // 1) 解析 --backend=gl|vk|null、--threading=...
    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown)
    {
        APP::SetBackend(GBackend::OpenGL);
    }

    const char* backendName =
        (APP::GetBackend() == GBackend::OpenGL) ? "OpenGL" :
        (APP::GetBackend() == GBackend::Vulkan) ? "Vulkan" : "Null";
    LOG_STREAM_INFO("001_Reflective_shadow_map") << "backend = " << backendName;

    // 2) 窗口/组件配置
    WINDOW_KEYWORD::SetWindowSize(1920, 1152);
    WINDOW_KEYWORD::SetIsCursorDisable(false);
    WINDOW_KEYWORD::SetWindowTitle("001_Reflective_shadow_map");
    COMPONENT_CONFIG::SetIsEnableGUI(true);

    // 3) 初始化（创建窗口 + 设备 + PassScheduler）
    APP::InitApp();

    // 3.1) ImGui Overlay：未注入业务回调 → IMGUI 模块自动调用 OVERLAY::Render，
    //      渲染默认"Renderer Info"面板（FPS / Backend / GPU / 分辨率）。
    //      如需追加业务侧自定义面板，可调用：
    //          OVERLAY::AddPanel("My Panel", [](){ ImGui::Text("..."); });
    //      若想完全替换默认面板，再调用：
    //          OVERLAY::SetDefaultPanelEnabled(false);

    // 4) 配置内置 FPS 飞行相机：
    //    sponza.obj 自身尺度约 ±2，地板 y≈-2，房间纵深 z≈±5。
    //    相机放在房间内 (0, 0, 4) 朝 -Z（yaw=-90）能看到沿 -Z 方向的内饰。
    //    aspect=0 → 跟随窗口；nearPlane/farPlane 与原 FlyCamera 一致。
    {
        CAMERA::FlyCameraConfig cfg{};
        cfg.position  = TitusMath::Vec3{0.0f, 0.0f, 4.0f};
        cfg.yawDeg    = -90.0f;
        cfg.pitchDeg  = 0.0f;
        cfg.fovDeg    = 60.0f;
        cfg.aspect    = 0.0f;        // 自动跟随窗口
        cfg.nearPlane = 0.1f;
        cfg.farPlane  = 100.0f;
        CAMERA::SetBuiltinFlyCameraConfig(cfg);
        CAMERA::EnableBuiltinFlyCamera(true);
    }

    // 5) AssetLoader 解码 → UploadGpuModel → 构造 Sponza
    //    严格分层：path → ModelAssetData（AssetLoader）→ GpuModelHandle（gfx）。
    //
    //    sponza.obj 的 UV 原本就是 OpenGL 约定（V 朝上）。改造前的旧路径
    //    （Renderer/Model.cpp）assimp 不带 aiProcess_FlipUVs，配合 stb 图像
    //    上下翻转后采样 OK；新路径 AssetLoader 默认 flipUVs=true（aiProcess_FlipUVs）
    //    + flipVerticallyOnLoad=true，会把 V 翻转两次，等价于 UV 多了一次错位，
    //    最终在 GBuffer Pass 的 fragment shader 里采到 diffuse 纹理的"另一半"，
    //    其中黑边 / sRGB 边界区域会被采到导致颜色发黑。
    //    这里显式关闭 flipUVs，与旧路径行为一致。
    TitusAsset::ModelAssetData modelAsset{};
    TitusAsset::ModelLoadOptions modelOpts{};
    modelOpts.flipUVs = false;
    const std::string sponzaPath = std::string(SOLUTION_DIR) + "Model/sponza/sponza.obj";
    if (!TitusAsset::LoadModel(sponzaPath, modelAsset, modelOpts))
    {
        LOG_STREAM_ERROR("001_Reflective_shadow_map") << "failed to load Sponza model: " << sponzaPath;
        APP::ShutdownApp();
        return 1;
    }
    GpuModelHandle sponzaHandle = APP::UploadGpuModel(modelAsset);
    if (!sponzaHandle.IsValid())
    {
        LOG_STREAM_ERROR("001_Reflective_shadow_map") << "UploadGpuModel failed.";
        APP::ShutdownApp();
        return 1;
    }

    TitusMath::Mat4 sponzaModelMatrix{1.0f}; // sponza.obj 自身尺度约 ±2，不再额外缩放
    Sponza sponza(sponzaHandle, sponzaModelMatrix);

    // 6) 构造并注册 4 个 Pass（GBuffer → RSM → Shading → ScreenQuad）
    auto gbufferPass = std::make_shared<SponzaGBufferPass>();
    auto rsmPass = std::make_shared<RSMBufferPass>();
    auto shadingPass = std::make_shared<ShadingWithRSMPass>();
    auto screenPass = std::make_shared<ScreenQuadPass>();

    gbufferPass->SetSponza(&sponza);
    rsmPass->SetSponza(&sponza);

    APP::AddPass(gbufferPass);
    APP::AddPass(rsmPass);
    APP::AddPass(shadingPass);
    APP::AddPass(screenPass);

    // 7) 主循环：内置 FlyCamera 控制器已被 APP::UpdateApp 在 PollEvents 之后
    //    自动 Tick，业务侧无需手动驱动相机。
    while (!APP::ShouldClose())
    {
        APP::UpdateApp();
    }

    // 8) 退出：等 GPU 空闲、Pass 析构、Sponza 析构（DestroyGpuModel）、
    //         APP::ShutdownApp 释放 device/window/scheduler。
    APP::WaitIdle();
    APP::ShutdownApp();
    return 0;
}
