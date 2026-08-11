// ============================================================================
// RendererInterface - TitusGfx.cpp
// 门面层实现：把全局状态（backend / threading mode / device 实例）集中到
// 单文件 anonymous namespace 中；APP::InitApp 时通过 GDeviceFactory 创建
// 设备；APP::UpdateApp 内驱动 PassScheduler。
//
// 本文件只允许 include RendererCore（IWindow/PassScheduler/...）与
// 自身工厂头；**禁止**直接 include Renderer/GLDevice.h、RendererVK/VKDevice.h。
// ============================================================================
#include "TitusGfx.h"
#include "TitusGfxImGui.h"
#include "GDeviceFactory.h"

#include "RendererCore/GDevice.h"
#include "RendererCore/PassScheduler.h"
#include "RendererCore/IWindow.h"
#include "RendererCore/IRenderPass.h"
#include "RendererCore/GDescs.h"
#include "RendererCore/GpuModel.h"
#include "RendererCore/AssetGpuUploader.h"
#include "RendererCore/Tests/DeviceLifecycleTest.h"
#include "AssetLoader/AssetTypes.h"
#include "AssetLoader/ImageWriter.h"
#include "AssetLoader/Tests/AssetLoaderSmokeTest.h"

// Platform 模块提供跨后端 GLFW 包装（仅 RendererInterface 内部 include，
// 业务模块看不到 Platform/* 头）。
#include "Platform/GLFWWindow.h"

// VK 路径下 g.window 为 nullptr，UpdateApp 需要直接 glfwPollEvents
// 让 GLFW 事件循环运转（含窗口关闭事件）。
#include <GLFW/glfw3.h>

#include "TracySupport.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include "Logger.h"
#include "LogMath.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <any>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <vector>

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // 全局状态：仅文件可见；通过门面命名空间访问
    // ------------------------------------------------------------------------
    namespace
    {
        struct GlobalState
        {
            GBackend backend = GBackend::Unknown;
            GThreadingMode threading = GThreadingMode::Direct;
            int windowWidth = 1280;
            int windowHeight = 720;
            std::string windowTitle = "TitusApp";
            bool cursorDisable = false;
            bool enableGUI = true;
            bool threadingExplicitlySet = false;
            // Validation：与 RendererVK/Common.cpp 默认一致（Debug=on / Release=off），
            // 可被 --validation= / SetEnableValidation 覆盖；仅 Vulkan 后端消费。
#ifdef _DEBUG
            bool enableValidation = true;
#else
            bool enableValidation = false;
#endif

            std::unique_ptr<TitusPlatform::GLFWWindow> window;
            std::unique_ptr<GDevice> device;
            std::unique_ptr<PassScheduler> scheduler;
            bool passesInitialized = false;

            // -- 资产上传器（获取 device 后创建） --
            std::unique_ptr<AssetGpuUploader> uploader;

            // -- GpuModel 注册表：id → RendererCore::GpuModel --
            std::unordered_map<uint64_t, std::unique_ptr<GpuModel>> gpuModels;
            uint64_t nextGpuModelId = 1; // 0 保留为非法句柄

            // -- 共享数据黑板 --
            std::unordered_map<std::string, std::any> sharedData;

            // -- CameraService 状态 --
            TitusMath::Mat4 cameraView{1.0f};
            TitusMath::Mat4 cameraProj{1.0f};
            TitusMath::Vec3 cameraPos{0.0f, 0.0f, 0.0f};

            // -- 内置 FlyCamera 控制器状态 --
            // 仅当 flyCameraEnabled=true 时被 UpdateApp 每帧 Tick；
            // 派生量（front/right）由 yaw/pitch 在 Tick 内重算。
            bool flyCameraEnabled = false;
            TitusRHI::CAMERA::FlyCameraConfig flyCameraCfg{};
            TitusMath::Vec3 flyPos{0.0f, 0.0f, 4.0f};
            float flyYawDeg = -90.0f;
            float flyPitchDeg = 0.0f;
            bool flyDragging = false;
            double flyLastCursorX = 0.0;
            double flyLastCursorY = 0.0;
            std::chrono::steady_clock::time_point flyLastTickTime{};
            bool flyHasLastTickTime = false;

            // -- Screenshot --
            // screenshotAtSec < 0 表示 CLI 自动截图关闭。
            double screenshotAtSec = -1.0;
            bool screenshotQuitAfter = true;
            bool screenshotDone = false;
            std::string screenshotDir; // 空 = 默认 $(SolutionDir)<appName>/results/
            std::chrono::steady_clock::time_point appStartTime{};
            bool appStartTimeValid = false;

            bool captureThisFrame = false;
            bool captureImmediate = false;
            bool pendingHideCapture = false;
            bool quitAfterThisCapture = false;

            bool lastScreenshotOk = false;
            std::string lastScreenshotPath;
            std::string lastScreenshotMessage;
        };

        GlobalState& Get()
        {
            static GlobalState g;
            return g;
        }

        const char* BackendFileTag(GBackend b)
        {
            switch (b)
            {
            case GBackend::OpenGL: return "gl";
            case GBackend::Vulkan: return "vk";
            case GBackend::Null:   return "null";
            default:               return "unknown";
            }
        }

        std::string ResolveDefaultScreenshotDir()
        {
            namespace fs = std::filesystem;
            const std::string& appName = TitusBasic::Logger::Instance().GetAppName();
            const std::string folder = appName.empty() ? std::string("TitusApp") : appName;
#ifdef SOLUTION_DIR
            try
            {
                fs::path solDir = fs::path(SOLUTION_DIR);
                if (!solDir.empty() && fs::exists(solDir))
                    return (solDir / folder / "results").string();
            }
            catch (...) { /* fallthrough */ }
#endif
            return (fs::current_path() / "results").string();
        }

        std::string MakeScreenshotFilePath(const std::string& dir)
        {
            namespace fs = std::filesystem;
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm tmv{};
#if defined(_WIN32)
            localtime_s(&tmv, &t);
#else
            localtime_r(&t, &tmv);
#endif
            char ts[32] = {};
            std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tmv);

            std::ostringstream name;
            name << "shot_" << BackendFileTag(Get().backend) << "_" << ts << ".png";
            return (fs::path(dir) / name.str()).string();
        }

        bool PerformScreenshotCapture()
        {
            auto& g = Get();
            g.lastScreenshotOk = false;
            g.lastScreenshotPath.clear();
            g.lastScreenshotMessage.clear();

            if (!g.device)
            {
                g.lastScreenshotMessage = "no device";
                LOG_STREAM_ERROR("TitusRHI::APP") << "CaptureScreenshot: no device";
                return false;
            }

            std::vector<uint8_t> rgba;
            uint32_t w = 0, h = 0;
            if (!g.device->ReadbackBackbuffer(rgba, w, h))
            {
                g.lastScreenshotMessage = "readback failed";
                LOG_STREAM_ERROR("TitusRHI::APP") << "CaptureScreenshot: ReadbackBackbuffer failed";
                return false;
            }

            const std::string dir = g.screenshotDir.empty()
                                        ? ResolveDefaultScreenshotDir()
                                        : g.screenshotDir;
            const std::string path = MakeScreenshotFilePath(dir);
            if (!TitusAsset::SaveImage2DPNG(path, w, h, rgba.data()))
            {
                g.lastScreenshotMessage = "write PNG failed";
                LOG_STREAM_ERROR("TitusRHI::APP") << "CaptureScreenshot: SaveImage2DPNG failed: " << path;
                return false;
            }

            g.lastScreenshotOk = true;
            g.lastScreenshotPath = path;
            g.lastScreenshotMessage = path;
            LOG_STREAM_INFO("TitusRHI::APP") << "Screenshot saved: " << path;
            return true;
        }

        void OnBeforePresentScreenshot()
        {
            auto& g = Get();
            if (!g.captureThisFrame) return;
            g.captureThisFrame = false;

            const bool ok = PerformScreenshotCapture();
            if (g.quitAfterThisCapture)
            {
                g.quitAfterThisCapture = false;
                if (ok)
                    APP::RequestClose();
            }
        }

        // 默认线程模式：当前各后端均走 Direct（见下方说明）
        GThreadingMode PickDefaultThreading(GBackend b)
        {
            // 暂时把所有后端都默认设为 Direct。Vulkan 端的 GDeviceMainThread
            // （Threaded 包装层）尚未完整支持 ComputePipeline 创建/Dispatch 与
            // DescriptorSet 绑定，会在 device.Init 阶段触发空指针。在 Worker
            // 路径补齐前，VK 也走同步直调路径与 GL 对齐。用户可显式
            // 通过 `--threading=threaded` 启用旧路径用于回归测试。
            switch (b)
            {
            case GBackend::Vulkan: return GThreadingMode::Direct;
            case GBackend::OpenGL: return GThreadingMode::Direct;
            default: return GThreadingMode::Direct;
            }
        }

        // --------------------------------------------------------------------
        // FlyCamera Tick：在 UpdateApp 内 PollEvents 之后、DrawFrame 之前调用。
        // 仅当 g.flyCameraEnabled=true 时生效；消费 TitusRHI::INPUT_MANAGER
        // 状态后回写 g.cameraView / g.cameraProj / g.cameraPos。
        //
        // 策略：与原 001/main.cpp::FlyCamera 完全一致，跨后端复用同一份逻辑。
        // dt 由 chrono 自闭环维护：第一帧 dt=0 不动；之后 clamp 在 0.1s 上限
        // 避免暂停 / 最小化恢复时位置突跳。
        // --------------------------------------------------------------------
        void FlyCameraTickIfEnabled()
        {
            auto& g = Get();
            if (!g.flyCameraEnabled) return;

            const auto now = std::chrono::steady_clock::now();
            float dt = 0.0f;
            if (g.flyHasLastTickTime)
            {
                dt = std::chrono::duration<float>(now - g.flyLastTickTime).count();
                if (dt < 0.0f) dt = 0.0f;
                if (dt > 0.1f) dt = 0.1f;
            }
            g.flyLastTickTime = now;
            g.flyHasLastTickTime = true;

            const auto& cfg = g.flyCameraCfg;

            // —— 鼠标右键拖拽旋转 ——
            const bool dragBtn = (TitusRHI::INPUT_MANAGER::GetMouseButton(cfg.dragMouseButton) != 0);
            double cx = 0.0, cy = 0.0;
            TitusRHI::INPUT_MANAGER::GetCursorPos(cx, cy);
            if (dragBtn)
            {
                if (!g.flyDragging)
                {
                    g.flyDragging = true;
                    g.flyLastCursorX = cx;
                    g.flyLastCursorY = cy;
                }
                else
                {
                    const double dx = cx - g.flyLastCursorX;
                    const double dy = cy - g.flyLastCursorY;
                    g.flyLastCursorX = cx;
                    g.flyLastCursorY = cy;
                    g.flyYawDeg += static_cast<float>(dx) * cfg.mouseSensitivity;
                    g.flyPitchDeg -= static_cast<float>(dy) * cfg.mouseSensitivity;
                    g.flyPitchDeg = std::clamp(g.flyPitchDeg, -89.0f, 89.0f);
                }
            }
            else
            {
                g.flyDragging = false;
            }

            // —— 重算朝向向量 ——
            const float yawRad = TitusMath::radians(g.flyYawDeg);
            const float pitchRad = TitusMath::radians(g.flyPitchDeg);
            TitusMath::Vec3 front;
            front.x = std::cos(yawRad) * std::cos(pitchRad);
            front.y = std::sin(pitchRad);
            front.z = std::sin(yawRad) * std::cos(pitchRad);
            front = TitusMath::normalize(front);
            const TitusMath::Vec3 worldUp{0.0f, 1.0f, 0.0f};
            const TitusMath::Vec3 right = TitusMath::normalize(TitusMath::cross(front, worldUp));

            // —— 键盘平移 ——
            using namespace TitusRHI::INPUT_MANAGER;
            float speed = cfg.moveSpeed;
            if (GetKeyStatus(KEY_LSHIFT)) speed *= cfg.fastMultiplier;
            if (GetKeyStatus(KEY_LCTRL)) speed *= cfg.slowMultiplier;
            const float step = speed * dt;

            if (GetKeyStatus(KEY_W)) g.flyPos += front * step;
            if (GetKeyStatus(KEY_S)) g.flyPos -= front * step;
            if (GetKeyStatus(KEY_A)) g.flyPos -= right * step;
            if (GetKeyStatus(KEY_D)) g.flyPos += right * step;
            if (GetKeyStatus(KEY_E)) g.flyPos += worldUp * step;
            if (GetKeyStatus(KEY_Q)) g.flyPos -= worldUp * step;

            // —— 写回主相机状态 ——
            // aspect <=0 时跟随窗口；否则按 cfg 给定值。
            float aspect = cfg.aspect;
            if (aspect <= 0.0f)
            {
                const int w = (g.windowWidth > 0) ? g.windowWidth : 1;
                const int h = (g.windowHeight > 0) ? g.windowHeight : 1;
                aspect = static_cast<float>(w) / static_cast<float>(h);
            }

            g.cameraPos = g.flyPos;
            g.cameraView = TitusMath::lookAt(g.flyPos, g.flyPos + front, worldUp);
            g.cameraProj = TitusMath::perspective(TitusMath::radians(cfg.fovDeg), aspect,
                                                  cfg.nearPlane, cfg.farPlane);

            if (g.backend == GBackend::Vulkan)
            {
                g.cameraProj[1][1] *= -1.0f;
            }
        }
    } // namespace

    // ------------------------------------------------------------------------
    // APP
    // ------------------------------------------------------------------------
    namespace APP
    {
        void SetBackend(GBackend backend) { Get().backend = backend; }

        void SetThreadingMode(GThreadingMode mode)
        {
            Get().threading = mode;
            Get().threadingExplicitlySet = true;
        }

        void SetEnableValidation(bool enable) { Get().enableValidation = enable; }

        GBackend GetBackend() { return Get().backend; }
        GThreadingMode GetThreadingMode() { return Get().threading; }
        bool GetEnableValidation() { return Get().enableValidation; }

        // 解析命令行 `--backend=gl|vk|null`、`--threading=direct|threaded|nonthreaded`、
        // `--validation=on|off`（亦接受 true/false/1/0）
        void ParseCommandLine(int argc, char** argv)
        {
            for (int i = 1; i < argc; ++i)
            {
                const char* a = argv[i];
                if (a == nullptr) continue;
                if (std::strncmp(a, "--backend=", 10) == 0)
                {
                    const char* v = a + 10;
                    if (std::strcmp(v, "gl") == 0) SetBackend(GBackend::OpenGL);
                    else if (std::strcmp(v, "vk") == 0) SetBackend(GBackend::Vulkan);
                    else if (std::strcmp(v, "null") == 0) SetBackend(GBackend::Null);
                    else
                        LOG_STREAM_ERROR("TitusRHI") << "Unknown --backend value: " << v;
                }
                else if (std::strncmp(a, "--threading=", 12) == 0)
                {
                    const char* v = a + 12;
                    if (std::strcmp(v, "direct") == 0) SetThreadingMode(GThreadingMode::Direct);
                    else if (std::strcmp(v, "threaded") == 0) SetThreadingMode(GThreadingMode::Threaded);
                    else if (std::strcmp(v, "nonthreaded") == 0) SetThreadingMode(GThreadingMode::NonThreaded);
                    else
                        LOG_STREAM_ERROR("TitusRHI") << "Unknown --threading value: " << v;
                }
                else if (std::strncmp(a, "--validation=", 13) == 0)
                {
                    const char* v = a + 13;
                    if (std::strcmp(v, "on") == 0 || std::strcmp(v, "true") == 0 || std::strcmp(v, "1") == 0)
                        SetEnableValidation(true);
                    else if (std::strcmp(v, "off") == 0 || std::strcmp(v, "false") == 0 || std::strcmp(v, "0") == 0)
                        SetEnableValidation(false);
                    else
                        LOG_STREAM_ERROR("TitusRHI") << "Unknown --validation value: " << v
                                  << " (expected on|off|true|false|1|0)";
                }
                else if (std::strncmp(a, "--screenshot-at=", 16) == 0)
                {
                    const char* v = a + 16;
                    char* end = nullptr;
                    const double sec = std::strtod(v, &end);
                    if (end == v || sec < 0.0)
                    {
                        LOG_STREAM_ERROR("TitusRHI") << "Invalid --screenshot-at value: " << v;
                    }
                    else
                    {
                        Get().screenshotAtSec = sec;
                        LOG_STREAM_INFO("TitusRHI") << "Auto screenshot at " << sec << "s";
                    }
                }
                else if (std::strncmp(a, "--screenshot-dir=", 17) == 0)
                {
                    Get().screenshotDir = a + 17;
                    LOG_STREAM_INFO("TitusRHI") << "Screenshot dir override: " << Get().screenshotDir;
                }
                else if (std::strncmp(a, "--quit-after-screenshot=", 24) == 0)
                {
                    const char* v = a + 24;
                    if (std::strcmp(v, "on") == 0 || std::strcmp(v, "true") == 0 || std::strcmp(v, "1") == 0)
                        Get().screenshotQuitAfter = true;
                    else if (std::strcmp(v, "off") == 0 || std::strcmp(v, "false") == 0 || std::strcmp(v, "0") == 0)
                        Get().screenshotQuitAfter = false;
                    else
                        LOG_STREAM_ERROR("TitusRHI") << "Unknown --quit-after-screenshot value: " << v
                                  << " (expected on|off|true|false|1|0)";
                }
            }
        }

        void InitApp()
        {
            auto& g = Get();
            if (g.backend == GBackend::Unknown)
            {
                LOG_STREAM_WARN("TitusRHI::APP") << "InitApp: backend not set, defaulting to OpenGL";
                g.backend = GBackend::OpenGL;
            }
            if (!g.threadingExplicitlySet)
            {
                g.threading = PickDefaultThreading(g.backend);
            }

            // 1) 创建窗口
            //   - GL 后端：业务层创建 GLFWWindow（带 GL context），device 复用之；
            //   - VK 后端：业务层不创建窗口，由 VKDevice::OnInitBackend 内部
            //              通过自建 VkWindow（GLFW + VK_NO_API）完整管理。
            //              这是因为 VKDevice 期望的 native handle 是 VkWindow*，
            //              与 GLFWWindow 类型不兼容；同时双 GLFW 窗口会产生
            //              type confusion 与 input 路由错乱。
            //   - Null 后端：不需窗口。
            if (g.backend == GBackend::OpenGL)
            {
                g.window = std::make_unique<TitusPlatform::GLFWWindow>();
                WindowDesc wdesc{};
                wdesc.width = static_cast<uint32_t>(g.windowWidth);
                wdesc.height = static_cast<uint32_t>(g.windowHeight);
                wdesc.title = g.windowTitle;
                wdesc.backend = g.backend;
                if (!g.window->Init(wdesc))
                {
                    LOG_STREAM_ERROR("TitusRHI::APP") << "InitApp: window.Init failed";
                    g.window.reset();
                    return;
                }
                g.windowWidth = static_cast<int>(g.window->GetWidth());
                g.windowHeight = static_cast<int>(g.window->GetHeight());
            }
            // VK 后端的窗口完全由 VKDevice 接管。

            // 2) 创建设备
            g.device = TitusRHIInterface::GDeviceFactory::Create(g.backend, g.threading);
            if (!g.device)
            {
                LOG_STREAM_ERROR("TitusRHI::APP") << "InitApp: failed to create device for backend="
                    << static_cast<int>(g.backend);
                if (g.window)
                {
                    g.window->Shutdown();
                    g.window.reset();
                }
                return;
            }

            // 3) device.Init(window)：GDevice 基类 → OnInitBackend → OnInitSwapchain
            GDeviceDesc dDesc{};
            dDesc.backend = g.backend;
            dDesc.applicationName = g.windowTitle.c_str();
            dDesc.enableValidation = g.enableValidation;
            dDesc.framesInFlight = 2;
            LOG_STREAM_INFO("TitusRHI::APP")
                << "enableValidation = " << (dDesc.enableValidation ? "on" : "off")
                << " (Vulkan only)";
            // VK 后端自管 VkWindow，需要从业务侧获取期望的窗口尺寸
            dDesc.windowWidth = static_cast<uint32_t>(g.windowWidth);
            dDesc.windowHeight = static_cast<uint32_t>(g.windowHeight);
            if (!g.device->Init(dDesc, g.window.get()))
            {
                LOG_STREAM_ERROR("TitusRHI::APP") << "InitApp: device.Init failed";
                g.device.reset();
                if (g.window)
                {
                    g.window->Shutdown();
                    g.window.reset();
                }
                return;
            }

            // 4) PassScheduler
            g.scheduler = std::make_unique<PassScheduler>();
            g.scheduler->SetDevice(g.device.get());
            g.scheduler->SetBeforePresentCallback(&OnBeforePresentScreenshot);
            // InitAllPasses 会依次调用已注册 Pass 的 Init(device)；
            // 未注册任何 Pass 时调用也是安全的（仅是空遍历）。
            g.scheduler->InitAllPasses();
            g.passesInitialized = true;

            // 5) AssetGpuUploader：供 APP::UploadGpuModel 使用
            g.uploader = std::make_unique<AssetGpuUploader>(g.device.get());

            // 6) ImGui Overlay：仅当 enableGUI=true 时初始化（方案 A）。
            //    在 device + scheduler 都就绪之后调用，以确保 IMGUI::Init 内部
            //    通过 GetGlobalDeviceForImGui() 拿到的 device 是有效的。
            //    Null 后端会在 IMGUI::Init 内被跳过；不影响主循环。
            if (g.enableGUI)
            {
                TitusRHI::IMGUI::Init();
            }

            g.appStartTime = std::chrono::steady_clock::now();
            g.appStartTimeValid = true;
        }

        void UpdateApp()
        {
            ZoneScopedN("APP::UpdateApp");
            auto& g = Get();
            if (g.window) g.window->PollEvents();
                // VK 后端 g.window 为 nullptr（由 VKDevice 自管 VkWindow），
                // 需要在这里统一 PollEvents 让 GLFW 事件循环运转。
            else glfwPollEvents();

            // 内置 FlyCamera 在 PollEvents 之后、DrawFrame 之前消费
            // 输入并刷新主相机；未启用时本调用为 no-op。
            FlyCameraTickIfEnabled();

            // 截图状态机：Hide-UI 延迟一帧 / CLI 定时 / 本帧立即预约
            bool skipImGui = false;
            bool wantCapture = false;
            bool wantQuitAfter = false;

            if (g.pendingHideCapture)
            {
                skipImGui = true;
                wantCapture = true;
                g.pendingHideCapture = false;
            }

            if (!g.screenshotDone && g.screenshotAtSec >= 0.0 && g.appStartTimeValid)
            {
                const double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - g.appStartTime).count();
                if (elapsed >= g.screenshotAtSec)
                {
                    wantCapture = true;
                    wantQuitAfter = g.screenshotQuitAfter;
                    g.screenshotDone = true;
                }
            }

            // 在 DrawFrame 之前完成 imgui NewFrame + 业务回调 +
            // ImGui::Render。生成的 draw data 在 DrawFrame 内部由 device 通过
            // RenderImGuiOverlay hook 录到 backbuffer / cmdbuf 上。
            // Hide-UI 截图帧：跳过 NewFrame，并禁用 Overlay 绘制以免沿用上帧 draw data。
            TitusRHI::IMGUI::SetOverlayDrawEnabled(!skipImGui);
            if (!skipImGui && g.enableGUI && TitusRHI::IMGUI::IsInitialized())
            {
                ZoneScopedN("IMGUI::NewFrame");
                TitusRHI::IMGUI::NewFrame();
            }

            if (g.captureImmediate)
            {
                wantCapture = true;
                g.captureImmediate = false;
            }

            g.captureThisFrame = wantCapture;
            g.quitAfterThisCapture = wantQuitAfter;

            if (g.device && g.scheduler) g.scheduler->DrawFrame();
            TitusRHI::IMGUI::SetOverlayDrawEnabled(true);

            // GL 后端需 SwapBuffers；VK 后端在 device.Present 内部完成、
            // GLFWWindow 在 VK 模式下的 SwapBuffers 为空操作。
            if (g.window) g.window->SwapBuffers();

            FrameMark;
        }

        void ShutdownApp()
        {
            auto& g = Get();
            if (g.device) g.device->WaitIdle();

            // 在 device.Shutdown 之前先 Shutdown imgui，
            // 让 imgui_impl_vulkan 在 VkDevice 仍有效时释放自身的 buffer/image/pipeline。
            TitusRHI::IMGUI::Shutdown();

            // 销毁所有仍在注册表中的 GpuModel（业务未显式 Destroy 的都在这里兑现）。
            // 顺序上要求在 device.Shutdown 之前、在所有 Pass 的 Destroy 之后（Pass 可能
            // 还在持有 GpuModelHandle）。
            if (g.scheduler && g.passesInitialized)
            {
                g.scheduler->DestroyAllPasses();
                g.passesInitialized = false;
            }
            if (g.device)
            {
                for (auto& kv : g.gpuModels)
                {
                    if (kv.second) kv.second->Release(*g.device);
                }
            }
            g.gpuModels.clear();
            g.uploader.reset();

            if (g.scheduler) g.scheduler.reset();
            if (g.device)
            {
                g.device->Shutdown();
                g.device.reset();
            }
            if (g.window)
            {
                g.window->Shutdown();
                g.window.reset();
            }
            g.sharedData.clear();
        }

        bool ShouldClose()
        {
            auto& g = Get();
            // 优先问上层 g.window（GL 路径）；若为空则转问 device
            // 是否自管窗口并已被关闭（VK 路径）。
            if (g.window) return g.window->ShouldClose();
            if (g.device) return g.device->IsWindowClosed();
            return true;
        }

        void RequestClose()
        {
            auto& g = Get();
            if (g.window)
            {
                if (GLFWwindow* win = static_cast<GLFWwindow*>(g.window->GetNativeHandle()))
                    glfwSetWindowShouldClose(win, GLFW_TRUE);
                return;
            }
            if (g.device)
            {
                if (GLFWwindow* win = static_cast<GLFWwindow*>(g.device->GetWindowNativeHandle()))
                    glfwSetWindowShouldClose(win, GLFW_TRUE);
            }
        }

        void WaitIdle()
        {
            auto& g = Get();
            if (g.device) g.device->WaitIdle();
        }

        bool CaptureScreenshot()
        {
            // 在 NewFrame / UpdateApp 中调用时预约到本帧 Present 前；
            // 若已错过本帧钩子则立即读回（GL 在 Swap 前仍可用；VK 可能失败）。
            auto& g = Get();
            g.captureImmediate = true;
            g.captureThisFrame = true;
            g.quitAfterThisCapture = false;
            return true;
        }

        void CaptureScreenshotNextFrameHideUi()
        {
            Get().pendingHideCapture = true;
        }

        bool GetLastScreenshotOk() { return Get().lastScreenshotOk; }
        const std::string& GetLastScreenshotPath() { return Get().lastScreenshotPath; }
        const std::string& GetLastScreenshotMessage() { return Get().lastScreenshotMessage; }

        void AddPass(const std::shared_ptr<IRenderPass> pass)
        {
            auto& g = Get();
            if (!pass) return;
            if (g.scheduler)
            {
                g.scheduler->AddPass(pass);
                if (g.passesInitialized && g.device)
                {
                    // 运行期追加：立即 Init 该单个 Pass
                    pass->Init(*g.device);
                }
            }
            else
            {
                // InitApp 之前调用：要求调用者在 InitApp 后再 AddPass。
                LOG_STREAM_WARN("TitusRHI::APP") << "AddPass called before InitApp; ignored";
            }
        }

        // -- GpuModel 上传 / 销毁 --
        GpuModelHandle UploadGpuModel(const TitusAsset::ModelAssetData& asset)
        {
            auto& g = Get();
            if (!g.device || !g.uploader)
            {
                LOG_STREAM_ERROR("TitusRHI::APP") << "UploadGpuModel called before InitApp";
                return GpuModelHandle{0};
            }
            auto model = std::make_unique<GpuModel>();
            AssetUploadOptions opts{};
            if (!model->LoadFromAsset(asset, *g.uploader, opts))
            {
                LOG_STREAM_ERROR("TitusRHI::APP") << "UploadGpuModel: LoadFromAsset failed";
                return GpuModelHandle{0};
            }
            const uint64_t id = g.nextGpuModelId++;
            g.gpuModels.emplace(id, std::move(model));
            return GpuModelHandle{id};
        }

        void DestroyGpuModel(GpuModelHandle handle)
        {
            auto& g = Get();
            if (!handle.IsValid()) return;
            auto it = g.gpuModels.find(handle.id);
            if (it == g.gpuModels.end()) return;
            if (g.device && it->second) it->second->Release(*g.device);
            g.gpuModels.erase(it);
        }

        const void* GetGpuModelInternal(GpuModelHandle handle)
        {
            if (!handle.IsValid()) return nullptr;
            auto& g = Get();
            auto it = g.gpuModels.find(handle.id);
            if (it == g.gpuModels.end()) return nullptr;
            return it->second.get();
        }

        int RunUnitTests()
        {
            // 路由到 RendererCore::Tests 里的 Null 后端测试入口。
            // 同时跑 AssetLoader 的纯 CPU 烟雾测试。
            // 业务侧仅看到 APP::RunUnitTests()，不需要任何 RendererCore include。
            int failures = 0;

            failures += TitusRHI::Tests::RunDeviceLifecycleTests();

            // SOLUTION_DIR 宏由各 vcxproj 通过 PreprocessorDefinitions 注入；
            // 不存在则退化为空字符串，烟雾测试会因找不到资源而 best-effort 跳过。
#ifdef SOLUTION_DIR
            failures += TitusAsset::Tests::RunAssetLoaderSmokeTests(SOLUTION_DIR);
#else
            failures += TitusAsset::Tests::RunAssetLoaderSmokeTests("");
#endif
            return failures;
        }
    } // namespace APP

    // ------------------------------------------------------------------------
    // WINDOW_KEYWORD
    // ------------------------------------------------------------------------
    namespace WINDOW_KEYWORD
    {
        void SetWindowSize(int width, int height, bool /*isViewportSizeChangedWithWindow*/)
        {
            auto& g = Get();
            g.windowWidth = width;
            g.windowHeight = height;
            // IWindow 接入后通过 Resize 通知后端。
        }

        void SetIsCursorDisable(bool isCursorDisable) { Get().cursorDisable = isCursorDisable; }
        int GetWindowWidth() { return Get().windowWidth; }
        int GetWindowHeight() { return Get().windowHeight; }
        void SetWindowTitle(const std::string& title) { Get().windowTitle = title; }
    }

    // ------------------------------------------------------------------------
    // COMPONENT_CONFIG
    // ------------------------------------------------------------------------
    namespace COMPONENT_CONFIG
    {
        void SetIsEnableGUI(bool isEnableGUI) { Get().enableGUI = isEnableGUI; }
    }

    // ------------------------------------------------------------------------
    // 内部访问器，供 RendererInterface/TitusGfxImGui.cpp 使用。
    // 业务侧不可见；通过 extern 在同一命名空间下解析。
    // ------------------------------------------------------------------------
    namespace IMGUI
    {
        IGDevice* GetGlobalDeviceForImGui()
        {
            return Get().device.get();
        }

        // GL 路径：g.window 持有 GLFWWindow（带 GL context），device 未实现
        // GetWindowNativeHandle。直接从全局状态返回 GLFWwindow*。
        // VK 路径：g.window 为 nullptr，由 device->GetWindowNativeHandle 提供。
        // 调用方应先尝试 device，再 fallback 到此。
        void* GetGlobalWindowForImGui()
        {
            auto& g = Get();
            if (g.window)
            {
                return g.window->GetNativeHandle();
            }
            return nullptr;
        }
    }

    // ------------------------------------------------------------------------
    // RESOURCE_MANAGER
    // ------------------------------------------------------------------------
    namespace RESOURCE_MANAGER
    {
        // 本文件中 IGRenderPass 等价于 RendererCore::IRenderPass；
        // 这里暂以前向声明形式占位，完整定义见 TitusGfxPass.h。
        void RegisterRenderPass(const std::shared_ptr<IGRenderPass>& /*pass*/)
        {
            // TODO：把 IGRenderPass 转成 std::shared_ptr<RendererCore::IRenderPass>
            // 后调用 g.scheduler->AddPass(...)。
            LOG_STREAM_WARN("TitusRHI::RESOURCE_MANAGER") << "RegisterRenderPass: stub";
        }

        void RemoveAllPasses()
        {
            auto& g = Get();
            if (g.scheduler) g.scheduler->RemoveAllPasses();
        }

        // -- 共享数据黑板底层存取（模板业务 API 在头文件内联定义） --
        namespace detail
        {
            const std::any* FindSharedDataAny(const std::string& name)
            {
                auto& g = Get();
                auto it = g.sharedData.find(name);
                if (it == g.sharedData.end()) return nullptr;
                return &it->second;
            }

            void StoreSharedDataAny(const std::string& name, std::any value)
            {
                auto& g = Get();
                g.sharedData[name] = std::move(value);
            }
        } // namespace detail
    }

    // ------------------------------------------------------------------------
    // INPUT_MANAGER
    // ------------------------------------------------------------------------
    namespace INPUT_MANAGER
    {
        namespace
        {
            // 拿到当前 active 的 GLFWwindow*：
            //   - GL 路径：g.window 持有 GLFWWindow（GLFWwindow* via GetNativeHandle）
            //   - VK 路径：g.window 为 nullptr，由 g.device 自管 VkWindow → 通过
            //              IGDevice::GetWindowNativeHandle 拿到 GLFWwindow*。
            GLFWwindow* GetActiveGLFWWindow()
            {
                auto& g = Get();
                if (g.window)
                {
                    return static_cast<GLFWwindow*>(g.window->GetNativeHandle());
                }
                if (g.device)
                {
                    return static_cast<GLFWwindow*>(g.device->GetWindowNativeHandle());
                }
                return nullptr;
            }
        } // namespace

        int GetKeyStatus(int key)
        {
            GLFWwindow* w = GetActiveGLFWWindow();
            if (!w) return 0;
            const int s = glfwGetKey(w, key);
            return (s == GLFW_PRESS || s == GLFW_REPEAT) ? 1 : 0;
        }

        int GetMouseButton(int button)
        {
            GLFWwindow* w = GetActiveGLFWWindow();
            if (!w) return 0;
            return (glfwGetMouseButton(w, button) == GLFW_PRESS) ? 1 : 0;
        }

        void GetCursorPos(double& x, double& y)
        {
            GLFWwindow* w = GetActiveGLFWWindow();
            x = 0.0;
            y = 0.0;
            if (!w) return;
            glfwGetCursorPos(w, &x, &y);
        }

        void SetCursorDisabled(bool disabled)
        {
            auto& g = Get();
            g.cursorDisable = disabled;
            GLFWwindow* w = GetActiveGLFWWindow();
            if (!w) return;
            glfwSetInputMode(w, GLFW_CURSOR,
                             disabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }

        bool IsCursorDisabled()
        {
            return Get().cursorDisable;
        }
    }

    // ------------------------------------------------------------------------
    // CAMERA
    // ------------------------------------------------------------------------
    namespace CAMERA
    {
        // 当前 RendererCore 没有相机抽象；以静态值作为占位。
        static double s_fov = 60.0;
        double GetMainCameraFov() { return s_fov; }
        void SetMainCameraFov(double fov) { s_fov = fov; }

        TitusMath::Mat4 GetMainCameraViewMatrix() { return Get().cameraView; }

        TitusMath::Mat4 GetMainCameraProjectionMatrix()
        {
            return Get().cameraProj;
        }

        TitusMath::Vec3 GetMainCameraPosition() { return Get().cameraPos; }

        void SetMainCameraViewMatrix(const TitusMath::Mat4& view) { Get().cameraView = view; }
        void SetMainCameraProjectionMatrix(const TitusMath::Mat4& proj) { Get().cameraProj = proj; }
        void SetMainCameraPosition(const TitusMath::Vec3& pos) { Get().cameraPos = pos; }

        // -- 内置 FlyCamera 控制器 --
        void EnableBuiltinFlyCamera(bool enable)
        {
            auto& g = Get();
            g.flyCameraEnabled = enable;
            if (enable)
            {
                // 启用瞬间用 cfg 同步初始 pos/yaw/pitch；后续帧由 Tick 增量更新。
                g.flyPos = g.flyCameraCfg.position;
                g.flyYawDeg = g.flyCameraCfg.yawDeg;
                g.flyPitchDeg = g.flyCameraCfg.pitchDeg;
                // 重置时间累计与拖拽状态，避免上次启用残留导致首帧大跳。
                g.flyDragging = false;
                g.flyHasLastTickTime = false;
            }
        }

        bool IsBuiltinFlyCameraEnabled() { return Get().flyCameraEnabled; }

        void SetBuiltinFlyCameraConfig(const FlyCameraConfig& cfg)
        {
            auto& g = Get();
            g.flyCameraCfg = cfg;
            // 若控制器已启用，立刻把 pos/yaw/pitch 同步到 cfg 给定值
            // （便于业务在运行期切换初始视角 / 速度参数）。
            if (g.flyCameraEnabled)
            {
                g.flyPos = cfg.position;
                g.flyYawDeg = cfg.yawDeg;
                g.flyPitchDeg = cfg.pitchDeg;
            }
        }

        FlyCameraConfig GetBuiltinFlyCameraConfig() { return Get().flyCameraCfg; }
    }
}
