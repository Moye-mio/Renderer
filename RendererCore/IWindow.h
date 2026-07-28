#pragma once
// ============================================================================
// RendererCore - IWindow
// 后端无关的窗口抽象。参数中绝不出现 GLFWwindow* / VkSurfaceKHR 等具体类型。
// 后端实现通过 GetNativeHandle()（返回 void*）按各自方式解释原生窗口对象。
// 设计参考：RendererCore 设计方案 §4 Platform/IWindow、需求 6.1。
// ============================================================================
#include <cstdint>
#include <string>

#include "GEnums.h"

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // 窗口创建参数（后端无关）
    // ------------------------------------------------------------------------
    struct WindowDesc
    {
        uint32_t    width   = 1280;
        uint32_t    height  = 720;
        std::string title   = "TitusApp";
        bool        fullscreen = false;
        bool        resizable  = true;
        // 决定 GLFW 创建窗口时的 ClientAPI：OpenGL 或 NoAPI（Vulkan）
        GBackend  backend = GBackend::Unknown;
    };

    // ------------------------------------------------------------------------
    // IWindow —— 后端无关窗口抽象
    // ------------------------------------------------------------------------
    class IWindow
    {
    public:
        virtual ~IWindow() = default;

        // —— 生命周期 ——
        virtual bool Init(const WindowDesc& desc) = 0;
        virtual void Shutdown() = 0;

        // —— 事件 ——
        virtual void PollEvents() = 0;
        virtual bool ShouldClose() const = 0;

        // —— 几何 ——
        virtual uint32_t GetWidth()  const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual bool     IsResized() const = 0;
        virtual void ClearResizedFlag() = 0;

        // —— Present（GL 后端在此调用 glfwSwapBuffers；VK 后端为空操作）——
        virtual void SwapBuffers() = 0;

        // 平台原生窗口指针（GL 后端会返回 GLFWwindow*；VK 后端返回 VkWindow*）。
        // 业务层不应该调用此方法；仅供后端实现内部使用。
        virtual void* GetNativeHandle() const = 0;
    };
}
