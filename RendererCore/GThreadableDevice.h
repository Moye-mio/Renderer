#pragma once
// ============================================================================
// RendererCore - GThreadableDevice
// 在 GDevice 之上叠加"渲染线程感知"能力：
//   - PostInitBackend(onRenderThread) 在 Init 末尾被基类回调，子类可
//     在此处把当前线程登记为"OwnerThread"。
//   - AcquireThreadOwnership() / ReleaseThreadOwnership() 在多线程模式（M2 任务 6）
//     接入：GDeviceWorker 的工作线程在循环开始处 Acquire，结束时 Release。
//
// 任务 1 阶段：本类只提供线程归属字段与简单的 AssertOnRenderThread 校验，
// 真正的"主线程门面 / Worker"机制留给任务 6（GDeviceMainThread + Worker）。
//
// 设计参考：requirements.md 需求 3.2 / 11.2 / 11.3。
// ============================================================================
#include <atomic>
#include <thread>

#include "GDevice.h"

namespace TitusRHI
{
    class GThreadableDevice : public GDevice
    {
    public:
        GThreadableDevice();
        ~GThreadableDevice() override;

        // —— 线程归属管理 —— 
        // 接管/让渡当前 std::thread::id 为渲染线程归属
        virtual void AcquireThreadOwnership();
        virtual void ReleaseThreadOwnership();

        // 当前是否被本线程持有（用于断言）
        bool IsOwnedByCurrentThread() const;

    protected:
        // 重写：默认实现 ——「是否在创建时就把当前线程登记为 OwnerThread」由 Init 调用决定
        void PostInitBackend(bool onRenderThread) override;

        // 渲染线程断言：默认开发期校验，发布期可关闭
        void AssertOnRenderThread() const override;

    private:
        std::atomic<std::thread::id> m_ownerThread { std::thread::id{} };
    };
}
