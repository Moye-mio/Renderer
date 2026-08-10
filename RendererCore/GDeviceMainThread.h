#pragma once
// ============================================================================
// RendererCore - GDeviceMainThread
// 主线程门面：实现 GThreadableDevice 接口，把"帧控制"四件事 BeginFrame /
// AcquireCommandList / Submit / Present / WaitIdle 序列化到 CommandRingBuffer
// 中由 Worker 线程消费；其余资源 API 直接同步透传到 RealDevice（持锁），保证
// 现有跨后端代码无需改动即可在 Threaded 模式下工作。
// 最小可用版：
//   - Init / Shutdown / WaitIdle 透传到 RealDevice，自身不分配 backend；
//   - BeginFrame / Submit / Present 转成 GCommand 写入 stream；
//   - AcquireCommandList 同步阻塞调用 RealDevice；
//   - 资源 Create*/Destroy/UpdateBuffer/UpdateTexture：直接 override 公共 API，
//     转发到 RealDevice（持 m_resourceMutex 锁），后续可改为流式延迟创建。
// ============================================================================
#include <memory>
#include <mutex>

#include "GThreadableDevice.h"
#include "CommandRingBuffer.h"
#include "GDeviceWorker.h"

namespace TitusRHI
{
    class GDeviceMainThread final : public GThreadableDevice
    {
    public:
        explicit GDeviceMainThread(std::unique_ptr<GDevice> realDevice);
        ~GDeviceMainThread() override;

        GDeviceMainThread(const GDeviceMainThread&) = delete;
        GDeviceMainThread& operator=(const GDeviceMainThread&) = delete;

        // —— 帧控制：覆盖基类公共 API，转发到 stream ——
        void BeginFrame() override;
        RenderCommandList* AcquireCommandList() override;
        void Submit(RenderCommandList* cmd) override;
        void Present() override;
        void WaitIdle() override;
        uint32_t GetCurrentFrameIndex() const override;
        const GCaps& GetCaps() const override;
        GBackend GetBackend() const override;
        void OnWindowResized(uint32_t w, uint32_t h) override;

        // —— 资源公共 API：直接透传 RealDevice（持锁）——
        BufferHandle CreateBuffer(const BufferDesc& desc) override;
        TextureHandle CreateTexture(const TextureDesc& desc) override;
        SamplerHandle CreateSampler(const SamplerDesc& desc) override;
        ShaderHandle CreateShader(const ShaderDesc& desc) override;
        PipelineHandle CreatePipeline(const GraphicsPipelineDesc& desc) override;
        PipelineHandle CreatePipeline(const ComputePipelineDesc& desc) override;
        // 光追管线
        PipelineHandle CreatePipeline(const RayTracingPipelineDesc& desc) override;
        RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc) override;
        // 光追：透传到 RealDevice（持锁）
        AccelerationStructureHandle CreateAccelerationStructure(const AccelerationStructureDesc& desc) override;

        void Destroy(BufferHandle handle) override;
        void Destroy(TextureHandle handle) override;
        void Destroy(SamplerHandle handle) override;
        void Destroy(ShaderHandle handle) override;
        void Destroy(PipelineHandle handle) override;
        void Destroy(RenderTargetHandle handle) override;
        void Destroy(AccelerationStructureHandle handle) override;

        void UpdateBuffer(BufferHandle buffer,
                          const void* src,
                          size_t bytes,
                          size_t dstOffset = 0) override;
        void UpdateTexture(TextureHandle texture,
                           const TextureUploadDesc& upload) override;

        // —— 资源查询：转发到 RealDevice，避免上层拿到 Client 时看到空表 ——
        const RHIBuffer* FindBuffer(BufferHandle h) const override;
        const RHITexture* FindTexture(TextureHandle h) const override;
        const RHIShader* FindShader(ShaderHandle h) const override;

    protected:
        // —— 设备生命周期：透传到 RealDevice，并启动 Worker ——
        bool OnInitBackend(const GDeviceDesc& desc, IWindow* window) override;
        bool OnInitSwapchain(IWindow* window) override;
        void OnShutdownSwapchain() override;
        void OnShutdownBackend() override;
        void OnWaitIdleImpl() override;

        // —— 余下 *Impl()：永不被调用（资源公共 API 已在 Client 层 override）——
        bool CreateBufferImpl(uint64_t /*id*/, const BufferDesc& /*d*/) override { return false; }
        bool CreateTextureImpl(uint64_t /*id*/, const TextureDesc& /*d*/) override { return false; }
        bool CreateSamplerImpl(uint64_t /*id*/, const SamplerDesc& /*d*/) override { return false; }
        bool CreateShaderImpl(uint64_t /*id*/, const ShaderDesc& /*d*/) override { return false; }
        bool CreatePipelineImpl(uint64_t /*id*/, const GraphicsPipelineDesc& /*d*/) override { return false; }
        bool CreatePipelineImpl(uint64_t /*id*/, const ComputePipelineDesc& /*d*/) override { return false; }
        bool CreatePipelineImpl(uint64_t /*id*/, const RayTracingPipelineDesc& /*d*/) override { return false; }
        bool CreateRenderTargetImpl(uint64_t /*id*/, const RenderTargetDesc& /*d*/) override { return false; }

        void DeleteBufferImpl(uint64_t /*id*/) override
        {
        }

        void DeleteTextureImpl(uint64_t /*id*/) override
        {
        }

        void DeleteSamplerImpl(uint64_t /*id*/) override
        {
        }

        void DeleteShaderImpl(uint64_t /*id*/) override
        {
        }

        void DeletePipelineImpl(uint64_t /*id*/) override
        {
        }

        void DeleteRenderTargetImpl(uint64_t /*id*/) override
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

        RenderCommandList* AcquireCommandListImpl() override { return nullptr; }

        void SubmitImpl(RenderCommandList* /*c*/) override
        {
        }

        void PresentImpl() override
        {
        }

    private:
        // 帮助：把帧命令推入 stream
        void PushFrameCmd(GCommandKind kind, void* payload = nullptr);

        std::unique_ptr<GDevice> m_realDevice;
        std::unique_ptr<CommandRingBuffer> m_stream;
        std::unique_ptr<GDeviceWorker> m_worker;
        mutable std::mutex m_resourceMutex; // 保护资源同步通道
        bool m_workerStarted = false;
    };
}
