#pragma once
// ============================================================================
// Renderer (OpenGL) - GLError
// OpenGL 错误队列辅助：排空残留错误、查询单条错误。
// ============================================================================
#include <GL/glew.h>

namespace TitusGraphics
{
    // 排空驱动错误队列中所有挂起错误。
    // 在调用关键 GL API 前使用，避免旧错误被后续 glGetError 误判。
    inline void ClearGLErrors()
    {
        while (glGetError() != GL_NO_ERROR) {}
    }
}
