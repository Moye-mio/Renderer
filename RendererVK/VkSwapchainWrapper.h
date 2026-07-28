#pragma once
// ============================================================================
// VkSwapchainWrapper —— Vulkan 交换链封装
// 对 OpenGL 而言没有直接对应物；Vulkan 必须显式管理多缓冲与 Present
// 包含：Swapchain、Images、ImageViews、DepthBuffer、Framebuffers、RenderPass(默认)
// ============================================================================
#include <vulkan/vulkan.h>
#include <vector>
#include "RENDERER_VK_EXPORTS.h"

class VkContext;
class VkWindow;

class RENDERER_VK_DLLEXPORTS VkSwapchainWrapper
{
public:
    VkSwapchainWrapper()  = default;
    ~VkSwapchainWrapper() = default;

    void Init(VkContext& ctx, VkWindow& window);
    void Destroy(VkContext& ctx);

    // 窗口 resize 后重建
    void Recreate(VkContext& ctx, VkWindow& window);

    // ---- Getters ----
    VkSwapchainKHR              GetSwapchain()     const { return m_swapchain; }
    VkFormat                    GetImageFormat()   const { return m_imageFormat; }
    VkExtent2D                  GetExtent()        const { return m_extent; }
    VkRenderPass                GetDefaultRenderPass() const { return m_defaultRenderPass; }
    uint32_t                    GetImageCount()    const { return static_cast<uint32_t>(m_images.size()); }
    VkFramebuffer               GetFramebuffer(uint32_t i) const { return m_framebuffers[i]; }
    const std::vector<VkImageView>& GetImageViews() const { return m_imageViews; }

    // ImGui Overlay 专用 RenderPass / Framebuffer：
    //   color attachment: loadOp=LOAD（保留 ScreenQuadPass 的输出）+ initialLayout=PRESENT_SRC_KHR
    //                     + finalLayout=PRESENT_SRC_KHR；不带 depth attachment（imgui 不需要）。
    // 用途：在主 Pass 之后、Present 之前，对 swapchain 当前 image 叠加 ImGui 绘制。
    VkRenderPass                GetImGuiRenderPass()             const { return m_imguiRenderPass; }
    VkFramebuffer               GetImGuiFramebuffer(uint32_t i)  const { return m_imguiFramebuffers[i]; }

private:
    void CreateSwapchain(VkContext& ctx, VkWindow& window);
    void CreateImageViews(VkContext& ctx);
    void CreateDepthResources(VkContext& ctx);
    void CreateDefaultRenderPass(VkContext& ctx);
    void CreateFramebuffers(VkContext& ctx);
    void CreateImGuiRenderPass(VkContext& ctx);   // ImGui Overlay RP（loadOp=Load, 单 color attachment）
    void CreateImGuiFramebuffers(VkContext& ctx); // 对应每个 swapchain image 的 FB
    void Cleanup(VkContext& ctx);

    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR   ChoosePresentMode (const std::vector<VkPresentModeKHR>&   modes)   const;
    VkExtent2D         ChooseExtent      (const VkSurfaceCapabilitiesKHR& caps, VkWindow& window) const;
    VkFormat           FindDepthFormat   (VkContext& ctx) const;

    VkSwapchainKHR             m_swapchain        = VK_NULL_HANDLE;
    VkFormat                   m_imageFormat      = VK_FORMAT_UNDEFINED;
    VkExtent2D                 m_extent           = {0, 0};
    std::vector<VkImage>       m_images;
    std::vector<VkImageView>   m_imageViews;

    VkImage        m_depthImage        = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory  = VK_NULL_HANDLE;
    VkImageView    m_depthImageView    = VK_NULL_HANDLE;
    VkFormat       m_depthFormat       = VK_FORMAT_UNDEFINED;

    VkRenderPass                m_defaultRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>  m_framebuffers;

    // ImGui Overlay 专用资源（与 m_defaultRenderPass 平行）：
    VkRenderPass                m_imguiRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>  m_imguiFramebuffers;
};
