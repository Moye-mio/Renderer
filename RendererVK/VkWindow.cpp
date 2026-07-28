#include "VkWindow.h"
#include "Common.h"
#include <iostream>
#include "Logger.h"

void VkWindow::Init()
{
    if (!glfwInit())
    {
        LOG_STREAM_ERROR("VkWindow") << "glfwInit failed";
        return;
    }

    // Vulkan 不需要 OpenGL Context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(
        TitusVkGraphics::WINDOW_KEYWORD::WINDOW_WIDTH,
        TitusVkGraphics::WINDOW_KEYWORD::WINDOW_HEIGHT,
        TitusVkGraphics::WINDOW_KEYWORD::WINDOW_TITLE.c_str(),
        nullptr, nullptr);

    if (!m_window)
    {
        LOG_STREAM_ERROR("VkWindow") << "Failed to create GLFW window";
        glfwTerminate();
        return;
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, FramebufferResizeCallback);

    if (TitusVkGraphics::WINDOW_KEYWORD::CURSOR_DISABLE)
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void VkWindow::Terminate()
{
    if (m_window)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

VkSurfaceKHR VkWindow::CreateSurface(VkInstance instance) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, m_window, nullptr, &surface) != VK_SUCCESS)
    {
        LOG_STREAM_ERROR("VkWindow") << "glfwCreateWindowSurface failed";
        return VK_NULL_HANDLE;
    }
    return surface;
}

void VkWindow::GetFramebufferSize(int& width, int& height) const
{
    glfwGetFramebufferSize(m_window, &width, &height);
}

void VkWindow::FramebufferResizeCallback(GLFWwindow* window, int /*width*/, int /*height*/)
{
    if (auto* self = static_cast<VkWindow*>(glfwGetWindowUserPointer(window)))
        self->m_framebufferResized = true;
}
