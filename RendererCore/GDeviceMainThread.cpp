// ============================================================================
// RendererCore - GDeviceMainThread.cpp
// 主线程门面实现：帧控制走 stream，资源公共 API 直接同步透传到 RealDevice。
// ============================================================================
#include "GDeviceMainThread.h"
#include "GDevice.h"
#include "RenderCommandList.h"

#include "TracySupport.h"

#include <iostream>

namespace TitusRHI
{
    GDeviceMainThread::GDeviceMainThread(std::unique_ptr<GDevice> realDevice)
        : m_realDevice(std::move(realDevice))
          , m_stream(std::make_unique<CommandRingBuffer>())
    {
    }

    GDeviceMainThread::~GDeviceMainThread()
    {
        if (m_worker)
        {
            m_worker->Stop();
            m_worker.reset();
        }
        if (m_stream) m_stream->Close();
    }

    // ---------------- 设备生命周期 ----------------
    bool GDeviceMainThread::OnInitBackend(const GDeviceDesc& desc, IWindow* window)
    {
        if (!m_realDevice) return false;
        // RealDevice 的 Init 由主线程同步完成（VkInstance / Surface 等需主线程）
        if (!m_realDevice->Init(desc, window)) return false;

        // 启动 Worker 接管渲染线程所有权
        m_worker = std::make_unique<GDeviceWorker>(m_realDevice.get(), m_stream.get());
        m_worker->Start();
        m_workerStarted = true;
        return true;
    }

    bool GDeviceMainThread::OnInitSwapchain(IWindow* /*window*/)
    {
        // RealDevice 已在 Init 内创建好 swapchain；这里无需再做。
        return true;
    }

    void GDeviceMainThread::OnShutdownSwapchain()
    {
        // 由 RealDevice->Shutdown() 间接处理。
    }

    void GDeviceMainThread::OnShutdownBackend()
    {
        if (m_worker)
        {
            m_worker->Stop();
            m_worker.reset();
        }
        if (m_realDevice)
        {
            m_realDevice->Shutdown();
            m_realDevice.reset();
        }
        if (m_stream) m_stream->Close();
    }

    void GDeviceMainThread::OnWaitIdleImpl()
    {
        if (m_worker)
        {
            const uint64_t tick = m_worker->IncrementTick();
            GCommandHeader hdr{GCommandKind::WaitIdle, 0};
            m_stream->Push(hdr);
            m_stream->SubmitWrites();
            m_worker->WaitForTick(tick);
        }
        else if (m_realDevice)
        {
            m_realDevice->WaitIdle();
        }
    }

    // ---------------- 帧控制 ----------------
    void GDeviceMainThread::PushFrameCmd(GCommandKind kind, void* payload)
    {
        if (!m_stream || !m_worker) return;
        GCommandHeader hdr{kind, 0};
        m_stream->Push(hdr);
        if (kind == GCommandKind::Submit && payload != nullptr)
        {
            // payload = RenderCommandList*
            m_stream->PushBytes(&payload, sizeof(payload));
        }
        m_stream->SubmitWrites();
        m_worker->IncrementTick();
    }

    void GDeviceMainThread::BeginFrame()
    {
        ZoneScopedN("MainThread::BeginFrame");
        // 等待 Worker 处理完上一帧的所有命令（Submit/Present），避免本帧 Acquire
        // 到的 cmd buffer 仍被 Worker 使用。当前为简化同步策略；后续可引入
        // 客户端代理与更细粒度的 fence。
        if (m_worker)
        {
            ZoneScopedN("MainThread::WaitWorker");
            const uint64_t pendingTick = m_worker->IncrementTick();
            // 这里的 IncrementTick 仅用于读取当前累计 tick；本身不写命令，
            // 我们直接等到 m_tickProgress 追上 pendingTick - 1（即处理完所有
            // 已写入但未消费的命令）。
            m_worker->WaitForTick(pendingTick - 1);
        }

        PushFrameCmd(GCommandKind::BeginFrame);
    }

    RenderCommandList* GDeviceMainThread::AcquireCommandList()
    {
        ZoneScopedN("MainThread::AcquireCommandList");
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->AcquireCommandList() : nullptr;
    }

    void GDeviceMainThread::Submit(RenderCommandList* cmd)
    {
        ZoneScopedN("MainThread::Submit");
        PushFrameCmd(GCommandKind::Submit, static_cast<void*>(cmd));
    }

    void GDeviceMainThread::Present()
    {
        ZoneScopedN("MainThread::Present");
        PushFrameCmd(GCommandKind::Present);
    }

    void GDeviceMainThread::WaitIdle()
    {
        OnWaitIdleImpl();
    }

    uint32_t GDeviceMainThread::GetCurrentFrameIndex() const
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->GetCurrentFrameIndex() : 0u;
    }

    const GCaps& GDeviceMainThread::GetCaps() const
    {
        // GetCaps 仅在 Init 后只读，不必加锁。
        static const GCaps kFallback{};
        return m_realDevice ? m_realDevice->GetCaps() : kFallback;
    }

    GBackend GDeviceMainThread::GetBackend() const
    {
        return m_realDevice ? m_realDevice->GetBackend() : GBackend::Unknown;
    }

    void GDeviceMainThread::OnWindowResized(uint32_t w, uint32_t h)
    {
        // Resize 必须等 Worker 处理完前序帧后再做（避免 swapchain 竞态）
        if (m_worker)
        {
            const uint64_t tick = m_worker->IncrementTick();
            GCommandHeader hdr{GCommandKind::WaitIdle, 0};
            m_stream->Push(hdr);
            m_stream->SubmitWrites();
            m_worker->WaitForTick(tick);
        }
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->OnWindowResized(w, h);
    }

    // ---------------- 资源公共 API：透传 + 持锁 ----------------
    BufferHandle GDeviceMainThread::CreateBuffer(const BufferDesc& desc)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->CreateBuffer(desc) : BufferHandle{};
    }

    TextureHandle GDeviceMainThread::CreateTexture(const TextureDesc& desc)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->CreateTexture(desc) : TextureHandle{};
    }

    SamplerHandle GDeviceMainThread::CreateSampler(const SamplerDesc& desc)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->CreateSampler(desc) : SamplerHandle{};
    }

    ShaderHandle GDeviceMainThread::CreateShader(const ShaderDesc& desc)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->CreateShader(desc) : ShaderHandle{};
    }

    PipelineHandle GDeviceMainThread::CreatePipeline(const GraphicsPipelineDesc& desc)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->CreatePipeline(desc) : PipelineHandle{};
    }

    PipelineHandle GDeviceMainThread::CreatePipeline(const ComputePipelineDesc& desc)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->CreatePipeline(desc) : PipelineHandle{};
    }

    // 光追管线：透传到 RealDevice。
    PipelineHandle GDeviceMainThread::CreatePipeline(const RayTracingPipelineDesc& desc)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->CreatePipeline(desc) : PipelineHandle{};
    }

    RenderTargetHandle GDeviceMainThread::CreateRenderTarget(const RenderTargetDesc& desc)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->CreateRenderTarget(desc) : RenderTargetHandle{};
    }

    AccelerationStructureHandle GDeviceMainThread::CreateAccelerationStructure(const AccelerationStructureDesc& desc)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->CreateAccelerationStructure(desc) : AccelerationStructureHandle{};
    }

    void GDeviceMainThread::Destroy(BufferHandle h)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->Destroy(h);
    }

    void GDeviceMainThread::Destroy(TextureHandle h)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->Destroy(h);
    }

    void GDeviceMainThread::Destroy(SamplerHandle h)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->Destroy(h);
    }

    void GDeviceMainThread::Destroy(ShaderHandle h)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->Destroy(h);
    }

    void GDeviceMainThread::Destroy(PipelineHandle h)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->Destroy(h);
    }

    void GDeviceMainThread::Destroy(RenderTargetHandle h)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->Destroy(h);
    }

    void GDeviceMainThread::Destroy(AccelerationStructureHandle h)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->Destroy(h);
    }

    void GDeviceMainThread::UpdateBuffer(BufferHandle b, const void* src, size_t bytes, size_t dstOffset)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->UpdateBuffer(b, src, bytes, dstOffset);
    }

    void GDeviceMainThread::UpdateTexture(TextureHandle t, const TextureUploadDesc& upload)
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        if (m_realDevice) m_realDevice->UpdateTexture(t, upload);
    }

    // ---------------- 资源查询：转发到 RealDevice ----------------
    const RHIBuffer* GDeviceMainThread::FindBuffer(BufferHandle h) const
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->FindBuffer(h) : nullptr;
    }

    const RHITexture* GDeviceMainThread::FindTexture(TextureHandle h) const
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->FindTexture(h) : nullptr;
    }

    const RHIShader* GDeviceMainThread::FindShader(ShaderHandle h) const
    {
        std::lock_guard<std::mutex> lk(m_resourceMutex);
        return m_realDevice ? m_realDevice->FindShader(h) : nullptr;
    }
}
