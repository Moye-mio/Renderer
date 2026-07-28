// ============================================================================
// RendererInterface - TitusGfxOverlay.cpp
// 默认 ImGui 信息面板（FPS / Backend / GPU / 分辨率）+ AddPanel 扩展点。
// 详见 TitusGfxOverlay.h。
//
// 后端信息收集策略：
//   - Backend 名称：TitusRHI::APP::GetBackend()
//   - GPU 名称：
//       · OpenGL 后端：glGetString(GL_RENDERER)（一次性缓存）
//       · Vulkan 后端：vkGetPhysicalDeviceProperties(...).deviceName（一次性缓存）
//   - 窗口分辨率：WINDOW_KEYWORD::GetWindowWidth/Height
//   - FPS / 帧时：ImGui::GetIO().Framerate（imgui 内部维护的滑动平均）
// ============================================================================
#include "TitusGfxOverlay.h"
#include "TitusGfx.h"

#include "RendererCore/IGDevice.h"
#include "RendererGL/GLDevice.h"
#include "RendererVK/VKDevice.h"
#include "RendererVK/VkContext.h"

#include <GL/glew.h>
#include <vulkan/vulkan.h>

#include <unordered_map>
#include <string>
#include <vector>

// 由 TitusGfx.cpp 在同命名空间下提供（已存在，IMGUI 模块也用过）。
namespace TitusRHI
{
    namespace IMGUI
    {
        TitusRHI::IGDevice* GetGlobalDeviceForImGui();
    }
}

namespace TitusRHI
{
    namespace OVERLAY
    {
        namespace
        {
            struct State
            {
                bool defaultEnabled = true;
                bool defaultVisible = true;

                // 自定义面板：用 vector<pair> 保持注册顺序稳定。
                std::vector<std::pair<std::string, PanelCallback>> panels;

                // GPU 名称缓存（首次 Render 时填充）。
                bool gpuNameCached = false;
                std::string gpuName;
            };

            State& g()
            {
                static State s;
                return s;
            }

            const char* BackendName(TitusRHI::GBackend b)
            {
                switch (b)
                {
                case TitusRHI::GBackend::OpenGL:  return "OpenGL";
                case TitusRHI::GBackend::Vulkan:  return "Vulkan";
                case TitusRHI::GBackend::Null:    return "Null";
                default:                              return "Unknown";
                }
            }

            // 首次调用时根据当前 backend / device 抓取 GPU 名并缓存。
            // 注意：必须在 device.Init 之后调用（GL context 已 current 才能 glGetString；
            // VK 物理设备已选）。Render 在 IMGUI NewFrame 之后才被调，已满足。
            void EnsureGpuNameCached()
            {
                State& s = g();
                if (s.gpuNameCached) return;

                using namespace TitusRHI;
                const GBackend backend = APP::GetBackend();

                if (backend == GBackend::OpenGL)
                {
                    const GLubyte* renderer = glGetString(GL_RENDERER);
                    const GLubyte* vendor   = glGetString(GL_VENDOR);
                    if (renderer)
                    {
                        s.gpuName = reinterpret_cast<const char*>(renderer);
                        if (vendor)
                        {
                            s.gpuName = std::string(reinterpret_cast<const char*>(vendor)) +
                                        " / " + s.gpuName;
                        }
                    }
                    else
                    {
                        s.gpuName = "(unknown GL renderer)";
                    }
                    s.gpuNameCached = true;
                }
                else if (backend == GBackend::Vulkan)
                {
                    auto* dev = TitusRHI::IMGUI::GetGlobalDeviceForImGui();
                    auto* vk  = dynamic_cast<TitusVkGraphics::VKDevice*>(dev);
                    if (vk && vk->GetVkContext())
                    {
                        VkPhysicalDevice pd = vk->GetVkContext()->GetPhysicalDevice();
                        if (pd != VK_NULL_HANDLE)
                        {
                            VkPhysicalDeviceProperties props{};
                            vkGetPhysicalDeviceProperties(pd, &props);
                            s.gpuName = props.deviceName;
                            s.gpuNameCached = true;
                        }
                    }
                    if (!s.gpuNameCached)
                    {
                        // device 还没装好；不缓存，下帧再试。
                        s.gpuName = "(querying VK GPU...)";
                    }
                }
                else
                {
                    s.gpuName = "(N/A)";
                    s.gpuNameCached = true;
                }
            }

            void DrawDefaultInfoPanel()
            {
                State& s = g();
                if (!s.defaultEnabled || !s.defaultVisible) return;

                EnsureGpuNameCached();

                ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize;
                if (!ImGui::Begin("Renderer Info", &s.defaultVisible, flags))
                {
                    ImGui::End();
                    return;
                }

                ImGuiIO& io = ImGui::GetIO();
                ImGui::Text("FPS:        %6.1f  (%.3f ms/frame)",
                            io.Framerate,
                            (io.Framerate > 0.0f) ? (1000.0f / io.Framerate) : 0.0f);

                ImGui::Separator();

                using namespace TitusRHI;
                ImGui::Text("Backend:    %s", BackendName(APP::GetBackend()));
                ImGui::Text("GPU:        %s", s.gpuName.c_str());
                ImGui::Text("Resolution: %d x %d",
                            WINDOW_KEYWORD::GetWindowWidth(),
                            WINDOW_KEYWORD::GetWindowHeight());

                if (APP::GetBackend() == GBackend::Vulkan)
                {
                    auto* dev = TitusRHI::IMGUI::GetGlobalDeviceForImGui();
                    auto* vk  = dynamic_cast<TitusVkGraphics::VKDevice*>(dev);
                    if (vk)
                    {
                        const auto st = vk->GetLastFrameDescriptorBindStats();
                        ImGui::Separator();
                        ImGui::Text("VK BindResourceSet (prev frame)");
                        ImGui::Text("  calls:      %u", st.bindResourceSetCalls);
                        ImGui::Text("  ds allocs:  %u", st.descriptorAllocs);
                        ImGui::Text("  cache hits: %u", st.descriptorCacheHits);
                    }
                }

                ImGui::End();
            }

            void DrawCustomPanels()
            {
                for (auto& kv : g().panels)
                {
                    if (!kv.second) continue;
                    if (ImGui::Begin(kv.first.c_str()))
                    {
                        kv.second();
                    }
                    ImGui::End();
                }
            }
        } // anonymous

        void SetDefaultPanelEnabled(bool enabled) { g().defaultEnabled = enabled; }
        bool IsDefaultPanelEnabled()              { return g().defaultEnabled; }

        void SetDefaultPanelVisible(bool visible) { g().defaultVisible = visible; }
        bool IsDefaultPanelVisible()              { return g().defaultVisible; }

        void AddPanel(const std::string& name, PanelCallback cb)
        {
            auto& v = g().panels;
            for (auto& kv : v)
            {
                if (kv.first == name) { kv.second = std::move(cb); return; }
            }
            v.emplace_back(name, std::move(cb));
        }

        void RemovePanel(const std::string& name)
        {
            auto& v = g().panels;
            for (auto it = v.begin(); it != v.end(); ++it)
            {
                if (it->first == name) { v.erase(it); return; }
            }
        }

        void ClearPanels()
        {
            g().panels.clear();
        }

        void Render()
        {
            DrawDefaultInfoPanel();
            DrawCustomPanels();
        }
    } // namespace OVERLAY
} // namespace TitusRHI
