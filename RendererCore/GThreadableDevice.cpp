// ============================================================================
// RendererCore - GThreadableDevice.cpp
// 线程归属字段与断言实现。任务 1 阶段保持极简：
//   - 默认 Init 同时承担"渲染线程"角色（单线程模式）
//   - 当前（M2）多线程模式下 GDeviceWorker 尚未接管线程所有权，owner 仍为调用
//     Init 的主线程；待 M3 任务 7 将资源调用也流化后，才由 Worker 在 Run()
//     入口调用 AcquireThreadOwnership 真正接管（详见 GDeviceWorker.cpp）。
// ============================================================================
#include "GThreadableDevice.h"

#include <cassert>
#include <iostream>
#include "Logger.h"

namespace TitusRHI
{
    GThreadableDevice::GThreadableDevice()  = default;
    GThreadableDevice::~GThreadableDevice() = default;

    void GThreadableDevice::AcquireThreadOwnership()
    {
        m_ownerThread.store(std::this_thread::get_id());
    }

    void GThreadableDevice::ReleaseThreadOwnership()
    {
        m_ownerThread.store(std::thread::id{});
    }

    bool GThreadableDevice::IsOwnedByCurrentThread() const
    {
        return m_ownerThread.load() == std::this_thread::get_id();
    }

    void GThreadableDevice::PostInitBackend(bool onRenderThread)
    {
        // 默认：如果 Init 由"将作为渲染线程的线程"调用，则立即接管归属。
        // M3 任务 7 将资源调用也流化后，改由 Worker 自己 AcquireThreadOwnership。
        (void)onRenderThread;
        AcquireThreadOwnership();
    }

    void GThreadableDevice::AssertOnRenderThread() const
    {
#ifdef _DEBUG
        if (!IsOwnedByCurrentThread())
        {
        LOG_STREAM_ERROR("GThreadableDevice") << "AssertOnRenderThread failed: "
                         "called from a non-owner thread\n";
            assert(false && "GThreadableDevice: not on render thread");
        }
#endif
    }
}
