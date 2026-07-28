#pragma once
// ============================================================================
// Platform - GLFWWindow
// IWindow 的 GLFW 单一实现：根据传入的 GBackend 决定
//   - GL  → glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API) 并 glfwMakeContextCurrent
//   - VK  → glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)
// 注意：本头文件 *不* 包含 <GLFW/glfw3.h>，仅在 .cpp 内引入；以保证业务侧
// 通过 RendererCore::IWindow 使用时不被迫拖入 GLFW 依赖。
// ============================================================================
#include <cstdint>
#include <string>

#include "RendererCore/IWindow.h"

// 前向声明，避免在头文件中暴露 GLFW
struct GLFWwindow;

namespace TitusPlatform
{
    class GLFWWindow : public TitusRHI::IWindow
    {
    public:
        GLFWWindow()  = default;
        ~GLFWWindow() override;

        GLFWWindow(const GLFWWindow&)            = delete;
        GLFWWindow& operator=(const GLFWWindow&) = delete;

        // —— IWindow 实现 ——
        bool     Init(const TitusRHI::WindowDesc& desc) override;
        void     Shutdown() override;

        void     PollEvents() override;
        bool     ShouldClose() const override;

        uint32_t GetWidth()  const override { return mWidth; }
        uint32_t GetHeight() const override { return mHeight; }
        bool     IsResized() const override { return mResized; }
        void     ClearResizedFlag() override { mResized = false; }

        void     SwapBuffers() override;
        void*    GetNativeHandle() const override { return mWindow; }

    private:
        static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);

        GLFWwindow*           mWindow  = nullptr;
        uint32_t              mWidth   = 0;
        uint32_t              mHeight  = 0;
        bool                  mResized = false;
        TitusRHI::GBackend mBackend = TitusRHI::GBackend::Unknown;
    };
}
