// ============================================================================
// RendererInterface - TitusGfxImGui.cpp
// 详见 TitusGfxImGui.h 的设计说明。
//
// 关键依赖（仅本 .cpp 内部使用）：
//   - imgui_master 仓库内的 imgui.h / imgui_impl_glfw.h / imgui_impl_opengl3.h /
//     imgui_impl_vulkan.h。源文件由 RendererInterface.vcxproj 直接编译。
//   - GLFWwindow*：通过 IGDevice::GetWindowNativeHandle 或本模块持有的
//     TitusPlatform::GLFWWindow* 拿到。
//   - VKDevice：dynamic_cast 后通过 GetVkContext / GetVkSwapchain /
//     GetCurrentPrimaryCommandBuffer / ImGuiBeginOneTimeCommands 等公开 API。
//
// 不在本文件做的事：
//   - imgui 自己的源码扩展或修改（保持 imgui_master 干净，便于后续升级）；
//   - 多 viewport / docking 等高级特性；
//   - 与现有 TitusRHI::INPUT_MANAGER 联动（imgui_impl_glfw 自带 callback 链路，
//     新框架的 INPUT_MANAGER 走 polling，互不干扰）。
// ============================================================================
#include "TitusGfxImGui.h"
#include "TitusGfx.h"
#include "TitusGfxOverlay.h"

#include "RendererCore/IGDevice.h"

// 后端具体头：本 .cpp 是 RendererInterface 内部"门面 + 后端粘合"的唯一允许位置，
// 对外仍只通过 TitusGfxImGui.h 暴露干净接口。
#include "RendererGL/GLDevice.h"
#include "RendererVK/VKDevice.h"
#include "RendererVK/VkContext.h"
#include "RendererVK/VkSwapchainWrapper.h"
#include "Platform/GLFWWindow.h"

// imgui 主体 + GLFW 平台层 + 两个渲染后端。
// 路径前缀与 RendererInterface.vcxproj 的 AdditionalIncludeDirectories 协同：
// 本模块新增 $(SolutionDir)Third-Party/imgui_master 与
// $(SolutionDir)Third-Party/imgui_master/examples 两个 include 目录。
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_vulkan.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "Logger.h"
#include <cstdint>

// 上面 IGDevice 已经声明了 GetWindowNativeHandle()；我们额外需要拿到
// TitusRHI::APP::GetBackend() 以及当前后端是 GL 还是 VK。

namespace
{
    // ------------------------------------------------------------------------
    // 模块内私有状态。Init/Shutdown 严格成对，NewFrame/RenderImGuiOverlay 之间
    // 由用户 UI 回调驱动 imgui 控件。
    // ------------------------------------------------------------------------
    struct State
    {
        bool initialized = false;
        TitusRHI::GBackend backend = TitusRHI::GBackend::Unknown;
        GLFWwindow* glfwWindow = nullptr;
        TitusRHI::IMGUI::UserCallback userCb;

        // VK 专用：自建的 DescriptorPool（imgui Vulkan backend 需要）。
        VkDevice vkDevice = VK_NULL_HANDLE;
        VkDescriptorPool vkDescPool = VK_NULL_HANDLE;
        // 缓存 device 指针，便于在 Render 回调里拿到 cmdbuf。
        TitusVkGraphics::VKDevice* vkDevicePtr = nullptr;
    };

    State& g()
    {
        static State s;
        return s;
    }

    // ------------------------------------------------------------------------
    // ImGui Overlay Render 回调
    //   - GL：直接 ImGui_ImplOpenGL3_RenderDrawData（默认 FB 绑定即可）
    //   - VK：device->RenderImGuiOverlay 已经把我们的 callback 包在了
    //         vkCmdBeginRenderPass + vkCmdEndRenderPass 之间，回调内只需要
    //         拿到当前帧 cmdbuf 后调 ImGui_ImplVulkan_RenderDrawData。
    // ------------------------------------------------------------------------
    void OverlayCallbackGL(void* /*userData*/)
    {
        ImDrawData* dd = ImGui::GetDrawData();
        if (dd) ImGui_ImplOpenGL3_RenderDrawData(dd);
    }

    void OverlayCallbackVK(void* /*userData*/)
    {
        if (!g().vkDevicePtr) return;
        VkCommandBuffer cb = g().vkDevicePtr->GetCurrentPrimaryCommandBuffer();
        if (cb == VK_NULL_HANDLE) return;
        ImDrawData* dd = ImGui::GetDrawData();
        if (dd) ImGui_ImplVulkan_RenderDrawData(dd, cb);
    }

    // ------------------------------------------------------------------------
    // 拿到当前 active 的 GLFWwindow*：与 INPUT_MANAGER 同源策略。
    //   - GL 路径：APP 持有 TitusPlatform::GLFWWindow → GetNativeHandle
    //   - VK 路径：device->GetWindowNativeHandle 返回 GLFWwindow*
    // 我们没法直接访问 TitusRHI::Get()，只能通过 IGDevice::GetWindowNativeHandle
    // 与 GDeviceFactory 已创建的 device 实例间接拿。
    // ------------------------------------------------------------------------

    // VK：创建 imgui 专用 DescriptorPool（按官方 example 风格的"豪华"配置）。
    bool CreateImGuiDescPoolVK(VkDevice device)
    {
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 },
        };
        VkDescriptorPoolCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        info.maxSets       = 1000 * (sizeof(poolSizes) / sizeof(poolSizes[0]));
        info.poolSizeCount = static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
        info.pPoolSizes    = poolSizes;
        VkResult res = vkCreateDescriptorPool(device, &info, nullptr, &g().vkDescPool);
        if (res != VK_SUCCESS)
        {
            LOG_STREAM_ERROR("IMGUI") << "vkCreateDescriptorPool failed: " << res;
            return false;
        }
        return true;
    }

    void CheckVkResultFn(VkResult err)
    {
        if (err != VK_SUCCESS)
        {
            LOG_STREAM_ERROR("IMGUI") << "vk error inside imgui_impl_vulkan: " << err;
        }
    }
}

namespace TitusRHI
{
    namespace IMGUI
    {
        void SetUserCallback(UserCallback cb)
        {
            g().userCb = std::move(cb);
        }

        bool IsInitialized() { return g().initialized; }

        void Init()
        {
            if (g().initialized) return;

            // 1) 拿 backend / device。device 必须存在（APP::InitApp 内 InitApp 之后调用）。
            const GBackend backend = APP::GetBackend();
            // RendererInterface 内部并未把 g.device 直接对外暴露 IGDevice*；
            // 这里通过私有"绕道"：所有 INPUT_MANAGER / 其他模块都是这样做的——
            // 我们不依赖外部 device 句柄，而是通过 TitusRHI::APP::* 做侧门。
            // 但 IMGUI 真的需要 IGDevice* —— 通过新增的 internal 访问器拿。
            // (该访问器在 TitusGfx.cpp 中实现：见下文 GetGlobalDevice。)
            extern IGDevice* GetGlobalDeviceForImGui();
            IGDevice* device = GetGlobalDeviceForImGui();
            if (!device)
            {
                LOG_STREAM_ERROR("IMGUI") << "Init failed: device not initialized";
                return;
            }

            // 2) 通用：取 GLFWwindow*（与 INPUT_MANAGER 同源策略）：
            //    优先问 device（VK 路径自管 VkWindow），不行就回退到全局 g.window（GL 路径）。
            GLFWwindow* win = static_cast<GLFWwindow*>(device->GetWindowNativeHandle());
            if (!win)
            {
                extern void* GetGlobalWindowForImGui();
                win = static_cast<GLFWwindow*>(GetGlobalWindowForImGui());
            }
            if (!win)
            {
                LOG_STREAM_ERROR("IMGUI") << "Init failed: cannot acquire GLFWwindow* from device or global window";
                return;
            }
            g().glfwWindow = win;
            g().backend    = backend;

            // 3) imgui 核心 context
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::StyleColorsDark();

            // 4) Platform binding（GLFW，install_callbacks=true 让 imgui 自接 char/key/scroll）
            const bool installCallbacks = true;
            if (backend == GBackend::OpenGL)
            {
                if (!ImGui_ImplGlfw_InitForOpenGL(win, installCallbacks))
                {
                    LOG_STREAM_ERROR("IMGUI") << "ImGui_ImplGlfw_InitForOpenGL failed";
                    ImGui::DestroyContext();
                    return;
                }
                if (!ImGui_ImplOpenGL3_Init("#version 330"))
                {
                    LOG_STREAM_ERROR("IMGUI") << "ImGui_ImplOpenGL3_Init failed";
                    ImGui_ImplGlfw_Shutdown();
                    ImGui::DestroyContext();
                    return;
                }
                device->SetImGuiOverlayCallback(&OverlayCallbackGL, nullptr);
            }
            else if (backend == GBackend::Vulkan)
            {
                auto* vk = dynamic_cast<TitusVkGraphics::VKDevice*>(device);
                if (!vk || !vk->GetVkContext() || !vk->GetVkSwapchain())
                {
                    LOG_STREAM_ERROR("IMGUI") << "Init failed: VKDevice / VkContext / Swapchain missing";
                    ImGui::DestroyContext();
                    return;
                }
                if (!ImGui_ImplGlfw_InitForVulkan(win, installCallbacks))
                {
                    LOG_STREAM_ERROR("IMGUI") << "ImGui_ImplGlfw_InitForVulkan failed";
                    ImGui::DestroyContext();
                    return;
                }

                VkContext* ctx = vk->GetVkContext();
                VkSwapchainWrapper* sc = vk->GetVkSwapchain();
                g().vkDevice    = ctx->GetDevice();
                g().vkDevicePtr = vk;

                if (!CreateImGuiDescPoolVK(g().vkDevice))
                {
                    ImGui_ImplGlfw_Shutdown();
                    ImGui::DestroyContext();
                    return;
                }

                ImGui_ImplVulkan_InitInfo init{};
                init.Instance        = ctx->GetInstance();
                init.PhysicalDevice  = ctx->GetPhysicalDevice();
                init.Device          = ctx->GetDevice();
                init.QueueFamily     = ctx->GetQueueFamilyIndices().graphicsFamily;
                init.Queue           = ctx->GetGraphicsQueue();
                init.PipelineCache   = VK_NULL_HANDLE;
                init.DescriptorPool  = g().vkDescPool;
                init.Allocator       = nullptr;
                init.CheckVkResultFn = &CheckVkResultFn;

                if (!ImGui_ImplVulkan_Init(&init, sc->GetImGuiRenderPass()))
                {
                    LOG_STREAM_ERROR("IMGUI") << "ImGui_ImplVulkan_Init failed";
                    vkDestroyDescriptorPool(g().vkDevice, g().vkDescPool, nullptr);
                    g().vkDescPool = VK_NULL_HANDLE;
                    ImGui_ImplGlfw_Shutdown();
                    ImGui::DestroyContext();
                    return;
                }

                // 上传 fonts texture（一次性 cmdbuf）
                VkCommandBuffer cb = vk->ImGuiBeginOneTimeCommands();
                if (cb != VK_NULL_HANDLE)
                {
                    ImGui_ImplVulkan_CreateFontsTexture(cb);
                    vk->ImGuiEndOneTimeCommands(cb);
                    ImGui_ImplVulkan_InvalidateFontUploadObjects();
                }

                device->SetImGuiOverlayCallback(&OverlayCallbackVK, nullptr);
            }
            else
            {
                // Null 后端等：跳过
                LOG_STREAM_INFO("IMGUI") << "Init skipped: backend not GL/VK";
                ImGui::DestroyContext();
                return;
            }

            g().initialized = true;
            LOG_STREAM_INFO("IMGUI") << "Init succeeded; backend = "
                << (backend == GBackend::OpenGL ? "OpenGL" : "Vulkan");
        }

        void NewFrame()
        {
            if (!g().initialized) return;

            // 1) 后端层 NewFrame
            if (g().backend == GBackend::OpenGL)
            {
                ImGui_ImplOpenGL3_NewFrame();
            }
            else if (g().backend == GBackend::Vulkan)
            {
                ImGui_ImplVulkan_NewFrame();
            }
            // 2) 平台层 NewFrame（共用）
            ImGui_ImplGlfw_NewFrame();

            // 3) imgui 核心 NewFrame
            ImGui::NewFrame();

            // 4) 业务侧 UI：
            //    - 默认（未注入用户回调）：调用 TitusRHI::OVERLAY::Render，
            //      渲染"Renderer Info"信息面板（FPS / Backend / GPU / 分辨率）
            //      + 业务侧通过 OVERLAY::AddPanel 注册的自定义面板。
            //    - 业务侧若调用 IMGUI::SetUserCallback 注入了自己的回调，则
            //      尊重该回调；业务侧也可在回调内自行调用 OVERLAY::Render
            //      来保留默认信息面板。
            if (g().userCb)
            {
                g().userCb();
            }
            else
            {
                TitusRHI::OVERLAY::Render();
            }

            // 5) 生成 draw data：在此调用 Render 后即可在后续的 Overlay
            //    回调中通过 ImGui::GetDrawData() 取到。
            ImGui::Render();
        }

        void Shutdown()
        {
            if (!g().initialized) return;

            if (g().backend == GBackend::OpenGL)
            {
                ImGui_ImplOpenGL3_Shutdown();
            }
            else if (g().backend == GBackend::Vulkan)
            {
                if (g().vkDevice != VK_NULL_HANDLE)
                {
                    vkDeviceWaitIdle(g().vkDevice);
                }
                ImGui_ImplVulkan_Shutdown();
                if (g().vkDescPool != VK_NULL_HANDLE && g().vkDevice != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(g().vkDevice, g().vkDescPool, nullptr);
                }
                g().vkDescPool  = VK_NULL_HANDLE;
                g().vkDevice    = VK_NULL_HANDLE;
                g().vkDevicePtr = nullptr;
            }

            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            g().initialized = false;
            g().backend     = GBackend::Unknown;
            g().glfwWindow  = nullptr;
        }
    } // namespace IMGUI
} // namespace TitusRHI
