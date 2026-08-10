#pragma once
// ============================================================================
// Renderer (OpenGL) - GLCommandList
// RenderCommandList 的 OpenGL 实现：
//   - 内部用 std::function 延迟队列录制 GL 调用（CPU 录制 → 主线程统一回放）
//   - BeginRenderPass = "BindFBO + 按 LoadOp 决定 glClear + 按 StoreOp 决定 glInvalidateFramebuffer" 模拟实现
// ============================================================================
#include <GL/glew.h>
#include <vector>
#include <functional>

#include "RendererCore/RenderCommandList.h"

namespace TitusGraphics
{
    class GLDevice;

    class GLCommandList : public TitusRHI::RenderCommandList
    {
    public:
        explicit GLCommandList(GLDevice* device);
        ~GLCommandList() override = default;

        // 由 GLDevice::BeginFrame 调用，清空待录制队列
        void Reset();
        // 由 GLDevice::Submit 调用，把整个队列回放到主线程的 GL 上下文
        void Replay();

        // ====================================================================
        // RenderCommandList 实现
        // ====================================================================
        void BeginRenderPass(const TitusRHI::RenderPassBeginInfo& info) override;
        void EndRenderPass() override;

        void SetViewport(const TitusRHI::Viewport& viewport) override;
        void SetScissor(const TitusRHI::Rect2D& scissor) override;

        void BindPipeline(TitusRHI::PipelineHandle pipeline) override;
        void BindVertexBuffer(uint32_t slot,
                              TitusRHI::BufferHandle buffer,
                              uint64_t offset = 0) override;
        void BindIndexBuffer(TitusRHI::BufferHandle buffer,
                             TitusRHI::IndexType indexType,
                             uint64_t offset = 0) override;

        void BindResourceSet(uint32_t setIndex,
                             const TitusRHI::ResourceSetDesc& setDesc) override;

        void PushConstants(TitusRHI::ShaderStage stages,
                           uint32_t offset,
                           uint32_t size,
                           const void* data) override;

        void Draw(uint32_t vertexCount,
                  uint32_t instanceCount = 1,
                  uint32_t firstVertex = 0,
                  uint32_t firstInstance = 0) override;
        void DrawIndexed(uint32_t indexCount,
                         uint32_t instanceCount = 1,
                         uint32_t firstIndex = 0,
                         int32_t vertexOffset = 0,
                         uint32_t firstInstance = 0) override;

        // 计算 + 屏障
        void Dispatch(uint32_t groupCountX,
                      uint32_t groupCountY,
                      uint32_t groupCountZ) override;
        void PipelineBarrier(const TitusRHI::PipelineBarrierDesc& desc) override;

    private:
        // 录制：把命令塞进延迟队列
        void Enqueue(std::function<void()> cmd);

    private:
        GLDevice* m_device = nullptr;
        std::vector<std::function<void()>> m_commands;

        // 当前绑定状态（录制阶段持有，用于 Draw 时确定拓扑/索引类型）
        TitusRHI::PrimitiveTopology m_currentTopology = TitusRHI::PrimitiveTopology::TriangleList;
        TitusRHI::IndexType m_currentIndexType = TitusRHI::IndexType::UInt32;
        uint64_t m_currentIndexOffset = 0;
        GLuint m_currentVAO = 0;
        GLuint m_currentProgram = 0;
        // 当前 Pipeline 是否为计算管线；Dispatch 需要该状态。
        bool m_currentIsCompute = false;
        // GL PushConstants 反射：当前 Pipeline 的 push_constant 布局。
        // GL 后端把 push_constant 语义拆为连续的“多个 uniform”。
        std::vector<TitusRHI::PushConstantRange> m_currentPushRanges;
        // 当前 Pipeline 的 vertex binding 表（binding 槽 → stride / 输入速率）。
        // BindVertexBuffer 用它做 glBindVertexBuffer(binding, vbo, offset, stride)。
        std::vector<TitusRHI::VertexBinding> m_currentVertexBindings;
    };
}
