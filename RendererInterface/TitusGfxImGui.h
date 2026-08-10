#pragma once
// ============================================================================
// RendererInterface - TitusGfxImGui
// 极薄的 ImGui 集成层（方案 A）：
//   - 内部封装 imgui_impl_glfw + imgui_impl_opengl3 / imgui_impl_vulkan，
//     根据当前 GBackend 自动分流；
//   - 不与 RendererCore / Renderer / RendererVK 头部强耦合：
//     仅 .cpp 内部 dynamic_cast 拿到具体 device 的访问能力；
//   - 业务侧通过 SetUserCallback 注入每帧 UI 内容（默认 ShowDemoWindow）；
//   - 由 APP::InitApp / UpdateApp / ShutdownApp 在合适时机调用：
//       Init   ：device.Init 之后；
//       NewFrame：每帧 PollEvents 之后、DrawFrame 之前；
//       Shutdown：scheduler 销毁之后、device.Shutdown 之前。
//   - 录制阶段：Init 时给 device 注入 RenderImGuiOverlay 回调，由 PassScheduler
//     在 Pass 录制结束、Submit 之前触发；
//     · GL：直接 ImGui_ImplOpenGL3_RenderDrawData（默认 FB 绑定即可）
//     · VK：在 swapchain 默认 RenderPass（loadOp=Load）内 ImGui_ImplVulkan_RenderDrawData
// ============================================================================
#include <functional>

// 业务侧编写 UI 回调时需要直接使用 ImGui:: API（如 ShowDemoWindow / Begin / End / SliderFloat）。
// 这里直接转出 imgui 主头，避免业务工程额外去 Third-Party/imgui_master/ 加 include path。
#include "imgui.h"

namespace TitusRHI
{
    namespace IMGUI
    {
        // 业务侧每帧 UI 回调：在 ImGui::NewFrame 之后、ImGui::Render 之前被调用。
        // 在回调内可以自由调用 ImGui::Begin/End、ImGui::ShowDemoWindow 等。
        using UserCallback = std::function<void()>;

        // 设置用户 UI 回调；可在 Init 之前或之后调用。
        void SetUserCallback(UserCallback cb);

        // 是否已成功初始化（即 enableGUI=true 且后端 init 成功）。
        bool IsInitialized();

        // 内部使用：由 RendererInterface 的 APP::InitApp / UpdateApp / ShutdownApp
        // 在统一时机调用。业务代码不应直接调用。
        void Init();
        void NewFrame();
        void Shutdown();
    }
}
