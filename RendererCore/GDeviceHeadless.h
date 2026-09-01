#pragma once
// ============================================================================
// RendererCore - GDeviceHeadless
// 空实现 GDevice：所有 *Impl() 钩子均为 no-op，但维持 Init/Frame/Destroy 流程
// 完整可用，便于：
//   1) 单元测试不依赖任何 GPU / 窗口；
//   2) Headless 跑批（如 CI 流水线、Pass 拓扑校验）；
//   3) 提前用 RendererInterface 流程冒烟（API 完整性 + 内存生命周期）。
// ============================================================================
#include <cstdint>
#include <cstring>

#include "GDevice.h"
#include "RenderCommandList.h"

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // NullCommandList —— RenderCommandList 的空实现
    // 全部 record 接口仅写入计数（便于断言："已记录 X 条命令"）
    // ------------------------------------------------------------------------
    class NullCommandList : public RenderCommandList
    {
    public:
        // RenderPass 控制
        void BeginRenderPass(const RenderPassBeginInfo&) override { ++m_drawCalls; }

        void EndRenderPass() override
        {
        }

        // 视口 / 裁剪
        void SetViewport(const Viewport&) override
        {
        }

        void SetScissor(const Rect2D&) override
        {
        }

        // 资源绑定
        void BindPipeline(PipelineHandle) override { ++m_pipelineBindings; }

        void BindVertexBuffer(uint32_t, BufferHandle, uint64_t) override
        {
        }

        void BindIndexBuffer(BufferHandle, IndexType, uint64_t) override
        {
        }

        void BindResourceSet(uint32_t, const ResourceSetDesc&) override
        {
        }

        void PushConstants(ShaderStage, uint32_t, uint32_t, const void*) override
        {
        }

        // 绘制
        void Draw(uint32_t v, uint32_t i, uint32_t, uint32_t) override
        {
            m_drawCalls += i ? i : 1;
            (void)v;
        }

        void DrawIndexed(uint32_t idx, uint32_t i, uint32_t, int32_t, uint32_t) override
        {
            m_drawCalls += i ? i : 1;
            (void)idx;
        }

        uint32_t GetDrawCallCount() const { return m_drawCalls; }
        uint32_t GetPipelineBindingCount() const { return m_pipelineBindings; }

        void ResetCounters()
        {
            m_drawCalls = 0;
            m_pipelineBindings = 0;
        }

    private:
        uint32_t m_drawCalls = 0;
        uint32_t m_pipelineBindings = 0;
    };

    // ------------------------------------------------------------------------
    // GDeviceHeadless —— Null 后端
    // ------------------------------------------------------------------------
    class GDeviceHeadless final : public GDevice
    {
    public:
        GDeviceHeadless() = default;
        ~GDeviceHeadless() override = default;

        GBackend GetBackend() const override { return GBackend::Null; }

    protected:
        bool OnInitBackend(const GDeviceDesc& /*desc*/, IWindow* /*window*/) override
        {
            // 填充最低限度的 Caps，让上层不至于因 Caps 全零而走异常分支
            m_caps.maxTextureSize2D = 4096;
            m_caps.maxTextureSize3D = 256;
            m_caps.maxTextureSizeCube = 4096;
            m_caps.maxColorAttachments = 8;
            m_caps.maxColorSampleCount = 8;
            m_caps.maxVertexAttributes = 16;
            m_caps.maxBoundDescriptorSets = 4;
            m_caps.supportsAnisotropy = false;
            m_caps.deviceName = "NullDevice";
            return true;
        }

        bool OnInitSwapchain(IWindow* /*window*/) override { return true; }

        void OnShutdownSwapchain() override
        {
        }

        void OnShutdownBackend() override
        {
        }

        void OnWaitIdleImpl() override
        {
        }

        bool CreateBufferImpl(uint64_t, const BufferDesc&) override { return true; }
        bool CreateTextureImpl(uint64_t, const TextureDesc&) override { return true; }
        bool CreateSamplerImpl(uint64_t, const SamplerDesc&) override { return true; }
        bool CreateShaderImpl(uint64_t, const ShaderDesc&) override { return true; }
        bool CreatePipelineImpl(uint64_t, const GraphicsPipelineDesc&) override { return true; }
        // Null 后端也接受 Compute Pipeline（不做任何事，用于单测）
        bool CreatePipelineImpl(uint64_t, const ComputePipelineDesc&) override { return true; }
        bool CreateRenderTargetImpl(uint64_t, const RenderTargetDesc&) override { return true; }

        void DeleteBufferImpl(uint64_t) override
        {
        }

        void DeleteTextureImpl(uint64_t) override
        {
        }

        void DeleteSamplerImpl(uint64_t) override
        {
        }

        void DeleteShaderImpl(uint64_t) override
        {
        }

        void DeletePipelineImpl(uint64_t) override
        {
        }

        void DeleteRenderTargetImpl(uint64_t) override
        {
        }

        void UpdateBufferImpl(BufferHandle, const void*, size_t, size_t) override
        {
        }

        void UpdateTextureImpl(TextureHandle, const TextureUploadDesc&) override
        {
        }

        void BeginFrameImpl() override
        {
        }

        RenderCommandList* AcquireCommandListImpl() override { return &m_cmd; }

        void SubmitImpl(RenderCommandList*) override
        {
        }

        void PresentImpl() override
        {
        }

    private:
        NullCommandList m_cmd;
    };
}
