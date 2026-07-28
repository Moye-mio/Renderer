#pragma once
// ============================================================================
// RendererVK - 通用配置与枚举
// 参考 OpenGL Renderer::Common.h 的设计语义，针对 Vulkan 后端做精简与重定义
// ============================================================================
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>
#include "RENDERER_VK_EXPORTS.h"
#include "Logger.h"

// 使用宏拼接解决方案目录与资源路径（与 OpenGL 版本保持一致）
#define VK_FONT_PATH(fontName) SOLUTION_DIR "Fonts/" fontName
#define VK_MODEL_PATH(modelName) SOLUTION_DIR "Model/" modelName
#define VK_SHADER_PATH(subProject, shaderName) SOLUTION_DIR subProject "/Shader/" shaderName

#ifndef VK_CHECK
#define VK_CHECK(expr)                                                                           \
    do                                                                                           \
    {                                                                                            \
        VkResult _vkRes = (expr);                                                                \
        if (_vkRes != VK_SUCCESS)                                                                \
        {                                                                                        \
            ::TitusBasic::Logger::Instance().Logf(::TitusBasic::LogLevel::Fatal,                 \
                "VkCheck", "%s failed with code %d @ %s:%d",                                  \
                #expr, static_cast<int>(_vkRes), __FILE__, __LINE__);                            \
            std::abort();                                                                        \
        }                                                                                        \
    } while (0)
#endif

namespace TitusVkGraphics
{
    namespace WINDOW_KEYWORD
    {
        extern int WINDOW_WIDTH;
        extern int WINDOW_HEIGHT;
        extern int VIEWPORT_LEFTBOTTOM_X;
        extern int VIEWPORT_LEFTBOTTOM_Y;
        extern bool CURSOR_DISABLE;
        extern std::string WINDOW_TITLE;
    }

    namespace COMPONENT_CONFIG
    {
        extern bool IS_ENABLE_GUI;
        extern bool ENABLE_VALIDATION_LAYER;
        extern uint32_t MAX_FRAMES_IN_FLIGHT; // 双缓冲/三缓冲
    }

    // ------------------------------------------------------------------------
    // 渲染通道事件枚举 —— 与 OpenGL 版本语义完全一致，便于示例代码迁移
    // ------------------------------------------------------------------------
    enum class ERenderPassEvent : int
    {
        BeforeRendering       = 0,
        BeforeShadowMap       = 50,
        ShadowMap             = 75,
        AfterShadowMap        = 100,
        BeforeGBuffer         = 150,
        GBuffer               = 175,
        AfterGBuffer          = 200,
        BeforeLighting        = 250,
        Lighting              = 275,
        AfterLighting         = 300,
        BeforeOpaqueShading   = 350,
        OpaqueShading         = 375,
        AfterOpaqueShading    = 400,
        BeforeTransparent     = 450,
        Transparent           = 475,
        AfterTransparent      = 500,
        BeforePostProcess     = 550,
        PostProcess           = 575,
        AfterPostProcess      = 600,
        BeforeFinalBlit       = 650,
        FinalBlit             = 675,
        AfterRendering        = 700,
    };

    // ------------------------------------------------------------------------
    // 基础 GPU 结构体
    // ------------------------------------------------------------------------
    struct QueueFamilyIndices
    {
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily  = UINT32_MAX;
        uint32_t computeFamily  = UINT32_MAX;
        uint32_t transferFamily = UINT32_MAX;

        bool IsComplete() const
        {
            return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
        }
    };

    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR        capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR>   presentModes;
    };
}
