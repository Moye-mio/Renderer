// ============================================================================
// RendererVK - VKDeviceFactory.cpp
// 提供给 RendererCore::GDeviceFactory 的桥接函数，避免 RendererCore 直接
// 依赖 RendererVK / Vulkan SDK。
// ============================================================================
#include <memory>

#include "VKDevice.h"
#include "RendererCore/IGDevice.h"

namespace TitusVkGraphics
{
    std::unique_ptr<TitusRHI::IGDevice> CreateVKDevice()
    {
        return std::make_unique<VKDevice>();
    }
}
