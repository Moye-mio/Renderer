#pragma once
// ============================================================================
// RendererCore - GDeviceFactory
// 静态工厂：根据 GBackend 返回对应后端的 IGDevice 实现。
// 实现细节：本头文件 *不* 直接 include 任何后端头；后端通过 cpp 中的
// 弱符号桥接函数 (`Create*Device`) 提供创建入口。被禁用的后端在 Create()
// 中返回 nullptr 并打印日志。
// ============================================================================
#include <memory>

#include "GEnums.h"

namespace TitusRHI
{
    class IGDevice;

    class GDeviceFactory
    {
    public:
        // 根据 backend 创建对应后端的设备实例。
        // 返回 std::unique_ptr，调用方持有所有权。
        // 若对应后端在编译期被禁用（未定义 RENDERER_ENABLE_GL/RENDERER_ENABLE_VK），
        // 则返回 nullptr 并写日志。
        static std::unique_ptr<IGDevice> Create(GBackend backend);
    };
}
