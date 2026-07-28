#include "VkCommandBufferWrapper.h"
#include "VkContext.h"
#include "Common.h"

void VkCommandBufferWrapper::Init(VkContext& ctx, VkCommandPool pool,
                                  VkCommandBufferLevel level, const std::string& name)
{
    m_name = name;
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool;
    ai.level              = level;
    ai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(ctx.GetDevice(), &ai, &m_cmdBuffer));
}

void VkCommandBufferWrapper::Begin(VkCommandBufferUsageFlags flags)
{
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = flags;
    VK_CHECK(vkBeginCommandBuffer(m_cmdBuffer, &bi));
}

void VkCommandBufferWrapper::End()
{
    VK_CHECK(vkEndCommandBuffer(m_cmdBuffer));
}

void VkCommandBufferWrapper::BeginRenderPass(VkRenderPass rp, VkFramebuffer fb,
                                             VkExtent2D extent,
                                             const std::vector<VkClearValue>& clears)
{
    VkRenderPassBeginInfo bi{};
    bi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass        = rp;
    bi.framebuffer       = fb;
    bi.renderArea.offset = {0, 0};
    bi.renderArea.extent = extent;
    bi.clearValueCount   = static_cast<uint32_t>(clears.size());
    bi.pClearValues      = clears.data();
    vkCmdBeginRenderPass(m_cmdBuffer, &bi, VK_SUBPASS_CONTENTS_INLINE);
}

void VkCommandBufferWrapper::EndRenderPass()
{
    vkCmdEndRenderPass(m_cmdBuffer);
}

void VkCommandBufferWrapper::SetViewport(float x, float y, float w, float h,
                                         float minD, float maxD)
{
    VkViewport vp{ x, y, w, h, minD, maxD };
    vkCmdSetViewport(m_cmdBuffer, 0, 1, &vp);
}

void VkCommandBufferWrapper::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    VkRect2D s{ {x, y}, {w, h} };
    vkCmdSetScissor(m_cmdBuffer, 0, 1, &s);
}

void VkCommandBufferWrapper::BindPipeline(VkPipeline pipeline, VkPipelineBindPoint bp)
{
    vkCmdBindPipeline(m_cmdBuffer, bp, pipeline);
}

void VkCommandBufferWrapper::Draw(uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi)
{
    vkCmdDraw(m_cmdBuffer, vc, ic, fv, fi);
}

void VkCommandBufferWrapper::DrawIndexed(uint32_t idxCnt, uint32_t instCnt,
                                         uint32_t firstIdx, int32_t vOff, uint32_t firstInst)
{
    vkCmdDrawIndexed(m_cmdBuffer, idxCnt, instCnt, firstIdx, vOff, firstInst);
}
