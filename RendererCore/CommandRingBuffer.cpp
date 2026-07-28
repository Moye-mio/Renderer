// ============================================================================
// RendererCore - CommandRingBuffer.cpp
// SPSC 字节环形缓冲实现。
// ============================================================================
#include "CommandRingBuffer.h"

#include <algorithm>
#include <cassert>

namespace TitusRHI
{
    size_t CommandRingBuffer::RoundUpPow2(size_t n)
    {
        if (n < 2) return 2;
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    CommandRingBuffer::CommandRingBuffer(size_t capacity)
    {
        m_capacity = RoundUpPow2(capacity);
        m_mask     = m_capacity - 1;
        m_buffer.resize(m_capacity);
    }

    CommandRingBuffer::~CommandRingBuffer()
    {
        Close();
    }

    void CommandRingBuffer::Close()
    {
        m_closed.store(true, std::memory_order_release);
    }

    // -- 生产者 --
    void CommandRingBuffer::WriteRaw(const void* src, size_t bytes)
    {
        if (bytes == 0) return;
        WaitForWriteSpace(bytes);
        if (m_closed.load(std::memory_order_acquire)) return;

        const size_t writePos = m_writePos.load(std::memory_order_relaxed);
        const size_t offset   = writePos & m_mask;
        const size_t firstChunk = std::min(bytes, m_capacity - offset);
        std::memcpy(m_buffer.data() + offset, src, firstChunk);
        if (firstChunk < bytes)
        {
            std::memcpy(m_buffer.data(), static_cast<const uint8_t*>(src) + firstChunk,
                        bytes - firstChunk);
        }
        m_writePos.store(writePos + bytes, std::memory_order_release);
    }

    void CommandRingBuffer::PushBytes(const void* data, size_t bytes)
    {
        WriteRaw(data, bytes);
    }

    void CommandRingBuffer::SubmitWrites()
    {
        // 当前实现下，WriteRaw 已经 release-store 了 m_writePos；本函数保留为
        // 显式同步点，方便未来加入"批次提交计数"或 cv 唤醒。
        std::atomic_thread_fence(std::memory_order_release);
    }

    // -- 消费者 --
    void CommandRingBuffer::ReadRaw(void* dst, size_t bytes)
    {
        if (bytes == 0) return;
        WaitForReadData(bytes);
        if (m_closed.load(std::memory_order_acquire) && !HasPendingData()) return;

        const size_t readPos = m_readPos.load(std::memory_order_relaxed);
        const size_t offset  = readPos & m_mask;
        const size_t firstChunk = std::min(bytes, m_capacity - offset);
        std::memcpy(dst, m_buffer.data() + offset, firstChunk);
        if (firstChunk < bytes)
        {
            std::memcpy(static_cast<uint8_t*>(dst) + firstChunk,
                        m_buffer.data(), bytes - firstChunk);
        }
        m_readPos.store(readPos + bytes, std::memory_order_release);
    }

    void CommandRingBuffer::PopBytes(void* dst, size_t bytes)
    {
        ReadRaw(dst, bytes);
    }

    void CommandRingBuffer::RetireReads()
    {
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    // -- 等待原语：spin → yield，避免 cv 带来的优先级反转 --
    void CommandRingBuffer::WaitForWriteSpace(size_t bytes)
    {
        // 缓冲已有字节数 = writePos - readPos；可写余量 = capacity - 已有字节数。
        for (int spin = 0;; ++spin)
        {
            const size_t writePos = m_writePos.load(std::memory_order_relaxed);
            const size_t readPos  = m_readPos .load(std::memory_order_acquire);
            const size_t used     = writePos - readPos;
            if (m_capacity - used >= bytes) return;
            if (m_closed.load(std::memory_order_acquire)) return;
            if (spin < 64) continue;
            std::this_thread::yield();
        }
    }

    void CommandRingBuffer::WaitForReadData(size_t bytes)
    {
        for (int spin = 0;; ++spin)
        {
            const size_t writePos = m_writePos.load(std::memory_order_acquire);
            const size_t readPos  = m_readPos .load(std::memory_order_relaxed);
            const size_t available = writePos - readPos;
            if (available >= bytes) return;
            if (m_closed.load(std::memory_order_acquire) && available == 0) return;
            if (spin < 64) continue;
            std::this_thread::yield();
        }
    }
}
