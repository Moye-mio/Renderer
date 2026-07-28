#pragma once
// ============================================================================
// RendererCore - HandleAllocator
// 基类共享的不透明 ID 分配器：单调递增；ID == 0 视为非法句柄。
// 作为 GDevice 的非虚成员存在；GL/VK 子类共用，避免重复实现。
// 设计参考：需求 3.1 / 3.3。
// ============================================================================
#include <atomic>
#include <cstdint>

namespace TitusRHI
{
    class HandleAllocator
    {
    public:
        HandleAllocator() = default;

        HandleAllocator(const HandleAllocator&)            = delete;
        HandleAllocator& operator=(const HandleAllocator&) = delete;

        // 申请一个新的非零 ID。线程安全。
        uint64_t Allocate()
        {
            return ++m_next;
        }

        uint64_t GetCurrent() const { return m_next.load(); }
        void     Reset()            { m_next.store(0); }

    private:
        std::atomic<uint64_t> m_next { 0 };
    };
}
