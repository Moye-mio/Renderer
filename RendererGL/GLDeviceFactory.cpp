// ============================================================================
// Renderer (OpenGL) - GLDeviceFactory.cpp
// 提供给 RendererCore::GDeviceFactory 的桥接函数。
// ============================================================================
#include <memory>

#include "GLDevice.h"
#include "RendererCore/IGDevice.h"

namespace TitusGraphics
{
    std::unique_ptr<TitusRHI::IGDevice> CreateGLDevice()
    {
        return std::make_unique<GLDevice>();
    }
}
