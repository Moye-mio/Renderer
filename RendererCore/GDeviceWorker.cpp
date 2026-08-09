// ============================================================================
// RendererCore - GDeviceWorker.cpp
// ============================================================================
#include "GDeviceWorker.h"

#include "GDevice.h"
#include "RenderCommandList.h"
#include "GThreadableDevice.h"

#include "TracySupport.h"

#include <iostream>

namespace TitusRHI
{
    GDeviceWorker::GDeviceWorker(GDevice* device, CommandRingBuffer* stream)
        : m_device(device)
        , m_stream(stream)
    {}

    GDeviceWorker::~GDeviceWorker()
    {
        if (m_running.load()) Stop();
    }

    void GDeviceWorker::Start()
    {
        if (m_running.load()) return;
        m_running.store(true);
        m_thread = std::thread(&GDeviceWorker::Run, this);
    }

    void GDeviceWorker::Stop()
    {
        if (!m_running.load()) return;
        // 写 Stop 命令；流缓冲不空时 Worker 会先消费完已积压的命令。
        if (m_stream)
        {
            GCommandHeader hdr{ GCommandKind::Stop, 0 };
            m_stream->Push(hdr);
            m_stream->SubmitWrites();
            ++m_tickCount;
        }
        m_running.store(false);
        if (m_thread.joinable()) m_thread.join();
    }

    void GDeviceWorker::Run()
    {
#ifdef TRACY_ENABLE
        tracy::SetThreadName("GDeviceWorker");
#endif
        // M2 最小可用版：**不**在这里 Acquire/Release 渲染线程所有权。
        // 原因：资源公共 API（CreateBuffer / Update 等）仍由 Client 在主线程
        // 同步调用 RealDevice，RealDevice 内部的 m_ownerThread 仍该为主线程，
        // 才不会触发调试断言。M3 任务 7 将所有资源调用也流化后，Worker
        // 才真正接管线程所有权。
        while (m_running.load() && m_stream && !m_stream->IsClosed())
        {
            const GCommandHeader hdr = m_stream->Pop<GCommandHeader>();
            m_stream->RetireReads();
            if (hdr.kind == GCommandKind::Stop) break;

            DispatchCommand(hdr);

            // 完成一条命令 → 推进 tick，唤醒等待者。
            m_tickProgress.fetch_add(1);
            {
                std::lock_guard<std::mutex> lk(m_tickMutex);
            }
            m_tickCv.notify_all();
        }
    }

    void GDeviceWorker::DispatchCommand(GCommandHeader header)
    {
        if (!m_device) return;
        switch (header.kind)
        {
        case GCommandKind::BeginFrame:
            m_device->BeginFrame();
            // AcquireCommandList 同步在 Submit 前由主线程读取共享指针；这里仅 Begin。
            break;
        case GCommandKind::Submit:
        {
            // 主线程在 Submit 之前已把 cmd 指针写入 stream（一个 void* 字段）。
            void* cmdPtr = nullptr;
            m_stream->PopBytes(&cmdPtr, sizeof(cmdPtr));
            m_stream->RetireReads();
            m_device->Submit(static_cast<RenderCommandList*>(cmdPtr));
            break;
        }
        case GCommandKind::Present:
            m_device->Present();
            break;
        case GCommandKind::WaitIdle:
            m_device->WaitIdle();
            break;
        case GCommandKind::Stop:
        default:
            break;
        }
    }

    void GDeviceWorker::WaitForTick(uint64_t waitTick)
    {
        std::unique_lock<std::mutex> lk(m_tickMutex);
        m_tickCv.wait(lk, [&]{
            return m_tickProgress.load(std::memory_order_acquire) >= waitTick
                || !m_running.load();
        });
    }
}
