// ============================================================================
// RendererInterface - GDeviceFactory.cpp
// 唯一同时 include GLDevice.h 与 VKDevice.h 的源文件。
// 其它任何源码都不得同时引用两端后端 SDK；本文件作为分发器。
// ============================================================================
#include "GDeviceFactory.h"

#include "RendererCore/GDevice.h"
#include "RendererCore/GDeviceMainThread.h"
#include "RendererCore/GDeviceHeadless.h"

#if defined(RENDERER_ENABLE_GL)
#include "RendererGL/GLDevice.h"
#endif

#if defined(RENDERER_ENABLE_VK)
#include "RendererVK/VKDevice.h"
#endif

#include <iostream>
#include "Logger.h"

namespace TitusRHIInterface
{
    using TitusRHI::GDevice;
    using TitusRHI::GBackend;
    using TitusRHI::GThreadingMode;

    std::unique_ptr<GDevice> GDeviceFactory::CreateRealDevice(GBackend backend)
    {
        switch (backend)
        {
        case GBackend::OpenGL:
        {
        #if defined(RENDERER_ENABLE_GL)
            return std::unique_ptr<GDevice>(new TitusGraphics::GLDevice());
        #else
        LOG_STREAM_ERROR("GDeviceFactory") << "OpenGL backend not enabled at compile time "
                         "(define RENDERER_ENABLE_GL to enable)\n";
            return nullptr;
        #endif
        }
        case GBackend::Vulkan:
        {
        #if defined(RENDERER_ENABLE_VK)
            return std::unique_ptr<GDevice>(new TitusVkGraphics::VKDevice());
        #else
        LOG_STREAM_ERROR("GDeviceFactory") << "Vulkan backend not enabled at compile time "
                         "(define RENDERER_ENABLE_VK to enable)\n";
            return nullptr;
        #endif
        }
        case GBackend::Null:
        {
            // 任务 10：返回 RendererCore::GDeviceHeadless
            // 该后端不依赖 GPU / 窗口，可用于单元测试 + Headless CI。
            return std::unique_ptr<GDevice>(new TitusRHI::GDeviceHeadless());
        }
        case GBackend::Unknown:
        default:
        LOG_STREAM_ERROR("GDeviceFactory") << "Unknown backend";
            return nullptr;
        }
    }

    std::unique_ptr<GDevice> GDeviceFactory::Create(GBackend backend, GThreadingMode threading)
    {
        // Threaded 模式：构造 RealDevice 后外包一层 GDeviceMainThread（任务 6 / M2）。
        // Direct / NonThreaded 模式：直接返回真实设备。
        if (threading == TitusRHI::GThreadingMode::Threaded)
        {
            auto real = CreateRealDevice(backend);
            if (!real)
            {
            LOG_STREAM_ERROR("GDeviceFactory") << "Threaded mode: real device creation failed; "
                             "falling back to nullptr.\n";
                return nullptr;
            }
            return std::unique_ptr<GDevice>(
                new TitusRHI::GDeviceMainThread(std::move(real)));
        }
        return CreateRealDevice(backend);
    }
}
