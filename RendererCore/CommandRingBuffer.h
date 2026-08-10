#pragma once
// ============================================================================
// RendererCore - CommandRingBuffer
// 单生产者-单消费者无锁环形字节流缓冲。
// 用途：GDeviceMainThread（生产者，主线程）把每条 API 序列化成 GCommand 写入；
// GDeviceWorker（消费者，渲染线程）逐字节解析并 dispatch 到真实设备。
// 关键不变量：
//   - 写指针 m_writePos 与读指针 m_readPos 都是 std::atomic<size_t>，仅各自线程
//     单调递增；二者之差 = 缓冲中的字节数。
//   - 读写线程通过两个原子量 + spin-wait（短暂忙等）实现等待 / 唤醒；
//     避免锁、cv 带来的优先级反转。
//   - 缓冲容量为 2 的幂；写满时阻塞写线程；读空时阻塞读线程。
// ============================================================================
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <thread>

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // CommandRingBuffer —— SPSC 字节环形缓冲
    // ------------------------------------------------------------------------
    class CommandRingBuffer
    {
    public:
        // capacity 必须是 2 的幂；构造时若不是会被向上对齐。
        explicit CommandRingBuffer(size_t capacity = (1u << 20)); // 默认 1 MiB
        ~CommandRingBuffer();

        CommandRingBuffer(const CommandRingBuffer&)            = delete;
        CommandRingBuffer& operator=(const CommandRingBuffer&) = delete;

        // ====================================================================
        // 生产者侧（仅主线程调用）
        // ====================================================================
        // 写入一个值类型；空间不足时忙等让出。
        template<typename T>
        void Push(const T& value)
        {
            static_assert(std::is_trivially_copyable<T>::value,
                          "Push requires trivially copyable type");
            WriteRaw(&value, sizeof(T));
        }

        // 写入任意字节数据（紧跟在前一次 Push 之后）。
        void PushBytes(const void* data, size_t bytes);

        // 表示一段命令写入完成；唤醒读线程。
        void SubmitWrites();

        // ====================================================================
        // 消费者侧（仅渲染线程调用）
        // ====================================================================
        // 读取一个值类型；当前可读字节不足时忙等让出。
        template<typename T>
        T Pop()
        {
            static_assert(std::is_trivially_copyable<T>::value,
                          "Pop requires trivially copyable type");
            T v{};
            ReadRaw(&v, sizeof(T));
            return v;
        }

        // 读取任意字节数据。
        void PopBytes(void* dst, size_t bytes);

        // 当前一段命令读取完成；推进读指针。
        void RetireReads();

        // 是否还有可读字节（消费者可据此决定是否短暂 yield）
        bool HasPendingData() const
        {
            return m_writePos.load(std::memory_order_acquire)
                 > m_readPos.load(std::memory_order_acquire);
        }

        // 关闭：唤醒所有阻塞中的线程并停止接受新写入；用于 Worker shutdown。
        void Close();
        bool IsClosed() const { return m_closed.load(std::memory_order_acquire); }

    private:
        void WriteRaw(const void* src, size_t bytes);
        void ReadRaw (void*       dst, size_t bytes);

        // 等待至少 bytes 字节可写空间；spin + yield。
        void WaitForWriteSpace(size_t bytes);
        // 等待至少 bytes 字节可读数据；spin + yield。
        void WaitForReadData  (size_t bytes);

        static size_t RoundUpPow2(size_t n);

        std::vector<uint8_t> m_buffer;
        size_t               m_capacity = 0;     // 2 的幂
        size_t               m_mask     = 0;     // m_capacity - 1
        std::atomic<size_t>  m_writePos{0};
        std::atomic<size_t>  m_readPos {0};
        std::atomic<bool>    m_closed  {false};
    };
}
