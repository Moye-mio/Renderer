#pragma once
// ============================================================================
// RendererCore - MockDevice
// 用于验证 IGDevice 的纯虚方法签名"可以被一个空实现完整覆盖"，
// 也是头文件能否独立编译的最小验证用例。
// 不参与生产构建（仅在 Tests 目录下编译）。
// ============================================================================
#include <atomic>

#include "IGDevice.h"
#include "IWindow.h"

namespace TitusRHI
{
    namespace Tests
    {
        // 简易的 ID 分配器：保证每次 Create 返回的句柄都不同（且 != 0）
        inline uint64_t AllocateMockId()
        {
            static std::atomic<uint64_t> sCounter{0};
            return ++sCounter;
        }

        class MockDevice : public IGDevice
        {
        public:
            // ------------------------------------------------------------------
            // 生命周期
            // ------------------------------------------------------------------
            bool Init(const GDeviceDesc& /*desc*/, IWindow* /*window*/) override { return true; }

            void Shutdown() override
            {
            }

            void WaitIdle() override
            {
            }

            void OnWindowResized(uint32_t /*w*/, uint32_t /*h*/) override
            {
            }

            // ------------------------------------------------------------------
            // 资源创建 / 销毁
            // ------------------------------------------------------------------
            BufferHandle CreateBuffer(const BufferDesc&) override { return BufferHandle{AllocateMockId()}; }
            TextureHandle CreateTexture(const TextureDesc&) override { return TextureHandle{AllocateMockId()}; }
            SamplerHandle CreateSampler(const SamplerDesc&) override { return SamplerHandle{AllocateMockId()}; }
            ShaderHandle CreateShader(const ShaderDesc&) override { return ShaderHandle{AllocateMockId()}; }
            PipelineHandle CreatePipeline(const GraphicsPipelineDesc&) override { return PipelineHandle{AllocateMockId()}; }
            // Compute Pipeline 重载
            PipelineHandle CreatePipeline(const ComputePipelineDesc&) override { return PipelineHandle{AllocateMockId()}; }
            RenderTargetHandle CreateRenderTarget(const RenderTargetDesc&) override { return RenderTargetHandle{AllocateMockId()}; }

            void Destroy(BufferHandle) override
            {
            }

            void Destroy(TextureHandle) override
            {
            }

            void Destroy(SamplerHandle) override
            {
            }

            void Destroy(ShaderHandle) override
            {
            }

            void Destroy(PipelineHandle) override
            {
            }

            void Destroy(RenderTargetHandle) override
            {
            }

            // ------------------------------------------------------------------
            // 数据上传
            // ------------------------------------------------------------------
            void UpdateBuffer(BufferHandle, const void*, size_t, size_t) override
            {
            }

            void UpdateTexture(TextureHandle, const TextureUploadDesc&) override
            {
            }

            // ------------------------------------------------------------------
            // 帧控制
            // ------------------------------------------------------------------
            void BeginFrame() override
            {
            }

            RenderCommandList* AcquireCommandList() override { return nullptr; }

            void Submit(RenderCommandList*) override
            {
            }

            void Present() override
            {
            }

            uint32_t GetCurrentFrameIndex() const override { return 0; }

            // ------------------------------------------------------------------
            // 能力查询
            // ------------------------------------------------------------------
            GBackend GetBackend() const override { return GBackend::Unknown; }
            const GCaps& GetCaps() const override { return m_caps; }

        private:
            GCaps m_caps;
        };
    } // namespace Tests
} // namespace TitusRHI
