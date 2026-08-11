#include "VkSwapchainWrapper.h"
#include "VkContext.h"
#include "VkWindow.h"
#include "Common.h"
#include <algorithm>
#include <array>
#include <iostream>
#include "Logger.h"

void VkSwapchainWrapper::Init(VkContext& ctx, VkWindow& window)
{
    CreateSwapchain(ctx, window);
    CreateImageViews(ctx);
    CreateDepthResources(ctx);
    CreateDefaultRenderPass(ctx);
    CreateFramebuffers(ctx);
    CreateImGuiRenderPass(ctx);
    CreateImGuiFramebuffers(ctx);
}

void VkSwapchainWrapper::Destroy(VkContext& ctx)
{
    Cleanup(ctx);
    if (m_imguiRenderPass)
    {
        vkDestroyRenderPass(ctx.GetDevice(), m_imguiRenderPass, nullptr);
        m_imguiRenderPass = VK_NULL_HANDLE;
    }
    if (m_defaultRenderPass)
    {
        vkDestroyRenderPass(ctx.GetDevice(), m_defaultRenderPass, nullptr);
        m_defaultRenderPass = VK_NULL_HANDLE;
    }
}

void VkSwapchainWrapper::Recreate(VkContext& ctx, VkWindow& window)
{
    int w = 0, h = 0;
    window.GetFramebufferSize(w, h);
    while (w == 0 || h == 0) // 窗口最小化
    {
        window.GetFramebufferSize(w, h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(ctx.GetDevice());

    Cleanup(ctx); // 保留 RenderPass（default + imgui），其他销毁

    CreateSwapchain(ctx, window);
    CreateImageViews(ctx);
    CreateDepthResources(ctx);
    CreateFramebuffers(ctx);
    CreateImGuiFramebuffers(ctx); // imgui RP 保留，只重建 FB
}

void VkSwapchainWrapper::Cleanup(VkContext& ctx)
{
    for (auto fb : m_imguiFramebuffers) vkDestroyFramebuffer(ctx.GetDevice(), fb, nullptr);
    m_imguiFramebuffers.clear();

    for (auto fb : m_framebuffers) vkDestroyFramebuffer(ctx.GetDevice(), fb, nullptr);
    m_framebuffers.clear();

    if (m_depthImageView)   vkDestroyImageView(ctx.GetDevice(), m_depthImageView, nullptr);
    if (m_depthImage)       vkDestroyImage(ctx.GetDevice(), m_depthImage, nullptr);
    if (m_depthImageMemory) vkFreeMemory(ctx.GetDevice(), m_depthImageMemory, nullptr);
    m_depthImageView = VK_NULL_HANDLE;
    m_depthImage = VK_NULL_HANDLE;
    m_depthImageMemory = VK_NULL_HANDLE;

    for (auto v : m_imageViews) vkDestroyImageView(ctx.GetDevice(), v, nullptr);
    m_imageViews.clear();

    if (m_swapchain)
    {
        vkDestroySwapchainKHR(ctx.GetDevice(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

// ============================================================================
void VkSwapchainWrapper::CreateSwapchain(VkContext& ctx, VkWindow& window)
{
    auto support = ctx.QuerySwapchainSupport();
    VkSurfaceFormatKHR fmt = ChooseSurfaceFormat(support.formats);
    VkPresentModeKHR   pm  = ChoosePresentMode(support.presentModes);
    VkExtent2D         ext = ChooseExtent(support.capabilities, window);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
        imageCount = support.capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = ctx.GetSurface();
    ci.minImageCount    = imageCount;
    ci.imageFormat      = fmt.format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = ext;
    ci.imageArrayLayers = 1;
    // TRANSFER_SRC：允许 Present 前把 swapchain image 拷到 staging 做截图读回。
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    const auto& idx = ctx.GetQueueFamilyIndices();
    uint32_t queueIdx[] = { idx.graphicsFamily, idx.presentFamily };
    if (idx.graphicsFamily != idx.presentFamily)
    {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = queueIdx;
    }
    else
    {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform   = support.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = pm;
    ci.clipped        = VK_TRUE;
    ci.oldSwapchain   = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(ctx.GetDevice(), &ci, nullptr, &m_swapchain));

    uint32_t cnt = 0;
    vkGetSwapchainImagesKHR(ctx.GetDevice(), m_swapchain, &cnt, nullptr);
    m_images.resize(cnt);
    vkGetSwapchainImagesKHR(ctx.GetDevice(), m_swapchain, &cnt, m_images.data());

    m_imageFormat = fmt.format;
    m_extent      = ext;
}

void VkSwapchainWrapper::CreateImageViews(VkContext& ctx)
{
    m_imageViews.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        VkImageViewCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image    = m_images[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format   = m_imageFormat;
        ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.baseMipLevel   = 0;
        ci.subresourceRange.levelCount     = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount     = 1;
        VK_CHECK(vkCreateImageView(ctx.GetDevice(), &ci, nullptr, &m_imageViews[i]));
    }
}

void VkSwapchainWrapper::CreateDepthResources(VkContext& ctx)
{
    m_depthFormat = FindDepthFormat(ctx);

    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.extent.width  = m_extent.width;
    ii.extent.height = m_extent.height;
    ii.extent.depth  = 1;
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.format        = m_depthFormat;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateImage(ctx.GetDevice(), &ii, nullptr, &m_depthImage));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(ctx.GetDevice(), m_depthImage, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = ctx.FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(ctx.GetDevice(), &ai, nullptr, &m_depthImageMemory));
    vkBindImageMemory(ctx.GetDevice(), m_depthImage, m_depthImageMemory, 0);

    VkImageViewCreateInfo vi{};
    vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image    = m_depthImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = m_depthFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vi.subresourceRange.baseMipLevel = 0;
    vi.subresourceRange.levelCount   = 1;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount   = 1;
    VK_CHECK(vkCreateImageView(ctx.GetDevice(), &vi, nullptr, &m_depthImageView));
}

void VkSwapchainWrapper::CreateDefaultRenderPass(VkContext& ctx)
{
    VkAttachmentDescription color{};
    color.format         = m_imageFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format         = m_depthFormat;
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub{};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { color, depth };
    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = static_cast<uint32_t>(attachments.size());
    ci.pAttachments    = attachments.data();
    ci.subpassCount    = 1;
    ci.pSubpasses      = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(ctx.GetDevice(), &ci, nullptr, &m_defaultRenderPass));
}

void VkSwapchainWrapper::CreateFramebuffers(VkContext& ctx)
{
    m_framebuffers.resize(m_imageViews.size());
    for (size_t i = 0; i < m_imageViews.size(); ++i)
    {
        std::array<VkImageView, 2> attachments = { m_imageViews[i], m_depthImageView };
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = m_defaultRenderPass;
        ci.attachmentCount = static_cast<uint32_t>(attachments.size());
        ci.pAttachments    = attachments.data();
        ci.width           = m_extent.width;
        ci.height          = m_extent.height;
        ci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(ctx.GetDevice(), &ci, nullptr, &m_framebuffers[i]));
    }
}

// ============================================================================
// Choosers
// ============================================================================
VkSurfaceFormatKHR VkSwapchainWrapper::ChooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) const
{
    for (const auto& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }
    return formats[0];
}

VkPresentModeKHR VkSwapchainWrapper::ChoosePresentMode(
    const std::vector<VkPresentModeKHR>& modes) const
{
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VkSwapchainWrapper::ChooseExtent(
    const VkSurfaceCapabilitiesKHR& caps, VkWindow& window) const
{
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    int w = 0, h = 0;
    window.GetFramebufferSize(w, h);
    VkExtent2D e = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    e.width  = std::clamp(e.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

VkFormat VkSwapchainWrapper::FindDepthFormat(VkContext& ctx) const
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };
    for (auto fmt : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(ctx.GetPhysicalDevice(), fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
            LOG_STREAM_ERROR("VkSwapchainWrapper") << "No supported depth format";
    return VK_FORMAT_D32_SFLOAT;
}

// ----------------------------------------------------------------------------
// ImGui Overlay 专用 RenderPass / Framebuffer
//   - color attachment: loadOp=LOAD（保留前一个 RP 的输出）
//                       initialLayout=PRESENT_SRC_KHR（接续 m_defaultRenderPass 的 finalLayout）
//                       finalLayout=PRESENT_SRC_KHR（直接把图像交给 vkQueuePresentKHR）
//   - 不带 depth attachment（imgui 不需要 depth）
//   - 单 subpass，加 external→subpass 依赖确保前一个 RP 的写入对本 RP 可见
// ----------------------------------------------------------------------------
void VkSwapchainWrapper::CreateImGuiRenderPass(VkContext& ctx)
{
    VkAttachmentDescription color{};
    color.format         = m_imageFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;          // 关键：保留先前内容
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;     // 关键：接续 default RP finalLayout
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;     // imgui 直接交给 Present

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &colorRef;

    // 等待前一个 RP（default RP）的 color attachment 写完后再开始本 RP 的 color 写。
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &color;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(ctx.GetDevice(), &ci, nullptr, &m_imguiRenderPass));
}

void VkSwapchainWrapper::CreateImGuiFramebuffers(VkContext& ctx)
{
    m_imguiFramebuffers.resize(m_imageViews.size());
    for (size_t i = 0; i < m_imageViews.size(); ++i)
    {
        VkImageView att = m_imageViews[i];
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = m_imguiRenderPass;
        ci.attachmentCount = 1;
        ci.pAttachments    = &att;
        ci.width           = m_extent.width;
        ci.height          = m_extent.height;
        ci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(ctx.GetDevice(), &ci, nullptr, &m_imguiFramebuffers[i]));
    }
}
