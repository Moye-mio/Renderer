// ============================================================================
// RendererCore - GDeviceFactory.cpp
// 仅依赖 IGDevice 接口与桥接函数，避免 RendererCore 直接依赖任何后端 SDK。
//
// 桥接函数声明：
//   - extern std::unique_ptr<IGDevice> CreateVKDevice();
//   - extern std::unique_ptr<IGDevice> CreateGLDevice();
// 它们位于各后端模块的 .cpp 中（见 RendererVK/VKDeviceFactory.cpp 与
// Renderer/GLDeviceFactory.cpp），通过 weak link 在缺席后端时回退为 nullptr。
//
// 本仓库不使用 weak symbol，转而采用编译期宏开关：
//   - RENDERER_ENABLE_VK：启用 Vulkan 后端
//   - RENDERER_ENABLE_GL：启用 OpenGL 后端
// 当宏未定义时，对应分支返回 nullptr；当宏定义时，调用 cpp 内的桥接函数。
// ============================================================================
#include "GDeviceFactory.h"
#include "IGDevice.h"

#include <iostream>
#include "Logger.h"

#if defined(RENDERER_ENABLE_VK)
namespace TitusVkGraphics { extern std::unique_ptr<TitusRHI::IGDevice> CreateVKDevice(); }
#endif

#if defined(RENDERER_ENABLE_GL)
namespace TitusGraphics  { extern std::unique_ptr<TitusRHI::IGDevice> CreateGLDevice(); }
#endif

namespace TitusRHI
{
    std::unique_ptr<IGDevice> GDeviceFactory::Create(GBackend backend)
    {
        switch (backend)
        {
        case GBackend::Vulkan:
        {
        #if defined(RENDERER_ENABLE_VK)
            return TitusVkGraphics::CreateVKDevice();
        #else
        LOG_STREAM_ERROR("GDeviceFactory") << "Vulkan backend not enabled at compile time "
                         "(define RENDERER_ENABLE_VK to enable)\n";
            return nullptr;
        #endif
        }
        case GBackend::OpenGL:
        {
        #if defined(RENDERER_ENABLE_GL)
            return TitusGraphics::CreateGLDevice();
        #else
        LOG_STREAM_ERROR("GDeviceFactory") << "OpenGL backend not enabled at compile time "
                         "(define RENDERER_ENABLE_GL to enable)\n";
            return nullptr;
        #endif
        }
        case GBackend::Unknown:
        default:
        LOG_STREAM_ERROR("GDeviceFactory") << "Unknown backend";
            return nullptr;
        }
    }
}
