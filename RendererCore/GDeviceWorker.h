#pragma once
// ============================================================================
// RendererCore - GDeviceWorker
// 渲染线程工作者：拥有 RealDevice，从 CommandRingBuffer 中读出 GCommand，
// dispatch 到 RealDevice 的对应 *Impl()。
//
// M2 阶段最小可用版（Minimum Viable Threading）：
//   - 仅把"帧控制"四件事串行化到 Worker：BeginFrame / AcquireCommandList /
//     Submit / Present / WaitIdle。
//   - 资源创建 / 销毁 / 上传 仍由 Client 端"持互斥锁阻塞调用 RealDevice"——M3 任务 7
//     再迁移到 Stream 中。
// 这样在 VK 后端可以让录制 / 提交 / Present 的 vkQueue 调用脱离主线程，
// 主线程仅做 Pass.Update / Pass.Record（写命令缓冲到本帧 RenderCommandList）。
//
// 设计参考：requirements.md 需求 11.4 / 11.5 / 11.6 / 11.7 / 15.4。
// ============================================================================
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "CommandRingBuffer.h"

namespace TitusRHI
{
    class GDevice;
    class RenderCommandList;

    enum class GCommandKind : uint16_t
    {
        Stop = 0,
        BeginFrame,
        Submit,
        Present,
        WaitIdle,
    };

    // 一条最小命令头：仅 4 字节，跟随的负载视 kind 而定。
    struct GCommandHeader
    {
        GCommandKind kind;
        uint16_t       reserved = 0;
    };

    class GDeviceWorker
    {
    public:
        // device：真实设备（GLDevice / VKDevice / GDeviceHeadless），不持有所有权；
        // stream：与 Client 共享的 SPSC 字节流。
        GDeviceWorker(GDevice* device, CommandRingBuffer* stream);
        ~GDeviceWorker();

        GDeviceWorker(const GDeviceWorker&)            = delete;
        GDeviceWorker& operator=(const GDeviceWorker&) = delete;

        // 启动 Worker 线程；启动后 Worker 接管 device 的渲染线程所有权。
        void Start();
        // 主线程要求 Worker 退出：写入 Stop 命令并 join。
        void Stop();

        // 主线程等待 Worker 处理完 stream 中的所有命令（Frame 结束同步点）。
        // 通过递增 m_tickCount + cv 实现：Worker 每读完一条命令递增 m_tickProgress；
        // 等到 m_tickProgress >= waitTick 时返回。
        void WaitForTick(uint64_t waitTick);
        // 当前 Tick：每写入一条命令时由 Client 调用 ++m_tickCount 取得新值。
        uint64_t IncrementTick() { return ++m_tickCount; }

    private:
        void Run();
        void DispatchCommand(GCommandHeader header);

        GDevice*            m_device = nullptr;
        CommandRingBuffer* m_stream = nullptr;
        std::thread           m_thread;

        std::atomic<bool>     m_running{false};
        std::atomic<uint64_t> m_tickCount   {0}; // 主线程 push tick
        std::atomic<uint64_t> m_tickProgress{0}; // worker 完成的 tick

        std::mutex              m_tickMutex;
        std::condition_variable m_tickCv;
    };
}
