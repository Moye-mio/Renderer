#pragma once
// ============================================================================
// Renderer (OpenGL) - GLTranslate
// 集中实现 RendererCore 后端无关枚举到 OpenGL GLenum 的翻译。
// ============================================================================
#include <GL/glew.h>

#include "RendererCore/GEnums.h"

namespace TitusGraphics
{
    // ------------------------------------------------------------------------
    // Format → 内部格式 / 外部格式 / 数据类型
    // ------------------------------------------------------------------------
    GLenum   ToGLInternalFormat(TitusRHI::Format format);
    GLenum   ToGLDataFormat   (TitusRHI::Format format);
    GLenum   ToGLDataType     (TitusRHI::Format format);

    // ------------------------------------------------------------------------
    // 拓扑 / 索引类型
    // ------------------------------------------------------------------------
    GLenum   ToGLPrimitive (TitusRHI::PrimitiveTopology topology);
    GLenum   ToGLIndexType (TitusRHI::IndexType type);
    GLsizei  IndexTypeBytes(TitusRHI::IndexType type);

    // ------------------------------------------------------------------------
    // 比较 / 混合
    // ------------------------------------------------------------------------
    GLenum   ToGLCompareOp  (TitusRHI::CompareOp op);
    GLenum   ToGLBlendFactor(TitusRHI::BlendFactor factor);
    GLenum   ToGLBlendOp    (TitusRHI::BlendOp op);

    // ------------------------------------------------------------------------
    // 光栅化
    // ------------------------------------------------------------------------
    GLenum   ToGLCullMode  (TitusRHI::CullMode mode);
    GLenum   ToGLFrontFace (TitusRHI::FrontFace face);
    GLenum   ToGLPolygonMode(TitusRHI::PolygonMode mode);

    // ------------------------------------------------------------------------
    // 着色器阶段
    // ------------------------------------------------------------------------
    GLenum   ToGLShaderStage(TitusRHI::ShaderStage stage);

    // ------------------------------------------------------------------------
    // 采样器
    // ------------------------------------------------------------------------
    GLenum   ToGLMinFilter (TitusRHI::FilterMode minFilter, TitusRHI::MipmapMode mipMode, bool hasMipmap);
    GLenum   ToGLMagFilter (TitusRHI::FilterMode magFilter);
    GLenum   ToGLAddressMode(TitusRHI::AddressMode mode);
}
