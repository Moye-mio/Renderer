#pragma once
// ============================================================================
// VkWindow —— 基于 GLFW 的 Vulkan 窗口封装
// 对标 GLFWWindow，区别：不再创建 OpenGL Context，而是创建 VkSurfaceKHR
// ============================================================================
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include "RENDERER_VK_EXPORTS.h"

class RENDERER_VK_DLLEXPORTS VkWindow
{
public:
    VkWindow()  = default;
    ~VkWindow() = default;

    void Init();
    void Terminate();

    // 创建 Surface（需要已创建好的 VkInstance）
    VkSurfaceKHR CreateSurface(VkInstance instance) const;

    GLFWwindow* GetWindow() const        { return m_window; }
    bool        IsResized() const        { return m_framebufferResized; }
    void        ClearResizedFlag()       { m_framebufferResized = false; }
    void        GetFramebufferSize(int& width, int& height) const;

private:
    GLFWwindow* m_window = nullptr;
    bool        m_framebufferResized = false;

    static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
};
