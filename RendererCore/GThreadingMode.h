#pragma once
// ============================================================================
// RendererCore - GThreadingMode
// 渲染线程模式枚举：与 RendererInterfaceTitusRHI::GThreadingMode 同步。
// 之所以独立放到 RendererCore 而不是 RendererInterface，是因为
// GDeviceMainThread / GDeviceWorker 在 RendererCore 内部使用。
// ============================================================================
#include <cstdint>

namespace TitusRHI
{
    enum class GThreadingMode : uint8_t
    {
        Direct = 0,     // 主线程直接驱动设备（GL/VK 当前默认）
        NonThreaded,    // 单线程录制 + 单线程提交（无 Worker）
        Threaded,       // 主线程门面 + Worker 工作线程（可选；--threading=threaded）
    };
}
