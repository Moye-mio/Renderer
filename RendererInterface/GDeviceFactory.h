#pragma once
// ============================================================================
// RendererInterface - GDeviceFactory
// 高层工厂：根据 GBackend + GThreadingMode 创建 RendererCore::GDevice。
//   - 在 Threaded 模式下外包一层 GDeviceMainThread；
//   - 在 Direct 模式下直接返回真实 GLDevice/VKDevice；
//   - 在 Null 模式下返回 RendererCore::GDeviceHeadless。
//
// 实现文件 GDeviceFactory.cpp 是**唯一**同时 include `RendererGL/GLDevice.h` 与
// `RendererVK/VKDevice.h` 的源文件；其它任何源码都禁止同时引用两端 SDK。
// ============================================================================
#include <memory>

#include "TitusGfx.h"

namespace TitusRHI { class GDevice; }

namespace TitusRHIInterface
{
    class GDeviceFactory
    {
    public:
        // 根据 backend + threading 创建对应的设备实例。
        // 返回 std::unique_ptr，调用方持有所有权。
        // 若对应后端在编译期被禁用（未定义 RENDERER_ENABLE_GL/RENDERER_ENABLE_VK），
        // 返回 nullptr 并写日志（不抛异常）。
        static std::unique_ptr<TitusRHI::GDevice> Create(
            TitusRHI::GBackend       backend,
            TitusRHI::GThreadingMode threading);

        // 在 Threaded/NonThreaded 模式下也可只创建真实后端设备（不外包 Client）。
        // 该工厂主要供 GDeviceMainThread 内部使用。
        static std::unique_ptr<TitusRHI::GDevice> CreateRealDevice(
            TitusRHI::GBackend backend);
    };
}
