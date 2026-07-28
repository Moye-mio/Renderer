// ============================================================================
// Platform - GLFWWindow.cpp
// 单一 GLFW 实现，根据传入的 GBackend 切换 ClientAPI；GL 后端额外初始化 glew。
// ============================================================================
#include "GLFWWindow.h"

// 双保险：即使 vcxproj 漏配了 GLFW_INCLUDE_NONE，这里也强制让 GLFW
// 不主动 include <GL/gl.h>，避免与下面的 <GL/glew.h> 冲突
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

// 仅 GL 后端编译时启用 glew 初始化（避免 VK 单后端构建强制依赖 OpenGL SDK）
// 注意：必须在 <GLFW/glfw3.h> 之前 include，否则会触发
//   "gl.h included before glew.h" 的 #error
#if !defined(RENDERERCORE_DISABLE_GL)
#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>
#include <iostream>
#include "Logger.h"

namespace TitusPlatform
{
    using TitusRHI::GBackend;
    using TitusRHI::WindowDesc;

    GLFWWindow::~GLFWWindow()
    {
        Shutdown();
    }

    bool GLFWWindow::Init(const WindowDesc& desc)
    {
        mBackend = desc.backend;
        mWidth = desc.width;
        mHeight = desc.height;

        if (!glfwInit())
        {
            LOG_STREAM_ERROR("GLFWWindow") << "glfwInit failed";
            return false;
        }

        // 按后端类型设置 ClientAPI
        if (mBackend == GBackend::OpenGL)
        {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            // 方案 A：让默认 backbuffer 具备 sRGB 能力，与 VK 的 SRGB swapchain 对齐。
            // 配合 GLDevice 中 glEnable(GL_FRAMEBUFFER_SRGB)，由驱动统一做 linear→sRGB 编码，
            // 避免 ScreenQuad_FS 手动 pow(c, 1/2.2) 与硬件 sRGB 编码叠加导致 VK/GL 亮度不一致。
            glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
        }
        else
        {
            // Vulkan / Unknown：不创建 GL 上下文
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }
        glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);

        mWindow = glfwCreateWindow(static_cast<int>(desc.width),
                                   static_cast<int>(desc.height),
                                   desc.title.c_str(),
                                   desc.fullscreen ? glfwGetPrimaryMonitor() : nullptr,
                                   nullptr);
        if (!mWindow)
        {
            LOG_STREAM_ERROR("GLFWWindow") << "glfwCreateWindow failed";
            glfwTerminate();
            return false;
        }

        glfwSetWindowUserPointer(mWindow, this);
        glfwSetFramebufferSizeCallback(mWindow, &GLFWWindow::FramebufferResizeCallback);

        // 高 DPI 显示器上 framebuffer 像素尺寸可能 ≠ 逻辑窗口尺寸（如 1.5×/2.0×）。
        // OpenGL 的 glViewport 与 backbuffer 都以像素为单位，因此这里同步实际
        // framebuffer 像素尺寸；否则 SetViewport(逻辑尺寸) 只覆盖左下角一部分，
        // 表现为"画面只在左下角显示"。
        {
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(mWindow, &fbW, &fbH);
            if (fbW > 0 && fbH > 0)
            {
                mWidth = static_cast<uint32_t>(fbW);
                mHeight = static_cast<uint32_t>(fbH);
            }
        }

        // GL 后端：MakeContextCurrent + glewInit
        if (mBackend == GBackend::OpenGL)
        {
            glfwMakeContextCurrent(mWindow);
#if !defined(RENDERERCORE_DISABLE_GL)
            glewExperimental = GL_TRUE;
            const GLenum glewErr = glewInit();
            if (glewErr != GLEW_OK)
            {
            LOG_STREAM_ERROR("GLFWWindow") << "glewInit failed: "
                    << reinterpret_cast<const char*>(glewGetErrorString(glewErr)) << '\n';
            }
#endif
            glfwSwapInterval(0);
        }

        return true;
    }

    void GLFWWindow::Shutdown()
    {
        if (mWindow)
        {
            glfwDestroyWindow(mWindow);
            mWindow = nullptr;
            glfwTerminate();
        }
    }

    void GLFWWindow::PollEvents()
    {
        glfwPollEvents();
    }

    bool GLFWWindow::ShouldClose() const
    {
        return mWindow ? glfwWindowShouldClose(mWindow) != 0 : true;
    }

    void GLFWWindow::SwapBuffers()
    {
        // 仅 OpenGL 后端需要 swap；Vulkan 后端的 Present 走 vkQueuePresentKHR（在 VKDevice 内）
        if (mBackend == GBackend::OpenGL && mWindow)
            glfwSwapBuffers(mWindow);
    }

    void GLFWWindow::FramebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
        if (!self) return;
        self->mWidth = static_cast<uint32_t>(width);
        self->mHeight = static_cast<uint32_t>(height);
        self->mResized = true;
    }
}
