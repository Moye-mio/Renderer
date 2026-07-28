#pragma once
// ============================================================================
// VkCommandBufferWrapper —— 封装真实的 VkCommandBuffer
// 对标 OpenGL 的 RenderCommandBuffer；区别：
//   - OpenGL 版本是 std::function 延迟队列（CPU 录制，主线程回放）
//   - Vulkan 版本直接录制到 GPU 命令缓冲，可以多线程并行录制（Secondary CmdBuf）
// 设计：每帧一个 Primary CommandBuffer（由 Scheduler 管理 per-frame-in-flight）
// ============================================================================
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include "RENDERER_VK_EXPORTS.h"

class VkContext;

class RENDERER_VK_DLLEXPORTS VkCommandBufferWrapper
{
public:
    VkCommandBufferWrapper()  = default;
    ~VkCommandBufferWrapper() = default;

    // 创建：从指定 CommandPool 分配
    void Init(VkContext& ctx, VkCommandPool pool,
              VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
              const std::string& name = "");

    // 释放（由 Pool 统一销毁，这里只清空句柄）
    void Reset() { if (m_cmdBuffer) vkResetCommandBuffer(m_cmdBuffer, 0); }

    // ---- 录制生命周期 ----
    void Begin(VkCommandBufferUsageFlags flags = 0);
    void End();

    // ---- 便捷 API ----
    void BeginRenderPass(VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent,
                         const std::vector<VkClearValue>& clears);
    void EndRenderPass();
    void SetViewport(float x, float y, float w, float h, float minD = 0.0f, float maxD = 1.0f);
    void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h);
    void BindPipeline(VkPipeline pipeline, VkPipelineBindPoint bp = VK_PIPELINE_BIND_POINT_GRAPHICS);
    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0);

    VkCommandBuffer    Get() const { return m_cmdBuffer; }
    const std::string& GetName() const { return m_name; }

private:
    VkCommandBuffer m_cmdBuffer = VK_NULL_HANDLE;
    std::string     m_name;
};
