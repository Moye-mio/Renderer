// ============================================================================
// Renderer (OpenGL) - GLTranslate.cpp
// 翻译表实现：把后端无关枚举映射为 OpenGL 的 GLenum。
// ============================================================================
#include "GLTranslate.h"

namespace TitusGraphics
{
    using TitusRHI::Format;
    using TitusRHI::PrimitiveTopology;
    using TitusRHI::IndexType;
    using TitusRHI::CompareOp;
    using TitusRHI::BlendFactor;
    using TitusRHI::BlendOp;
    using TitusRHI::CullMode;
    using TitusRHI::FrontFace;
    using TitusRHI::PolygonMode;
    using TitusRHI::ShaderStage;
    using TitusRHI::FilterMode;
    using TitusRHI::MipmapMode;
    using TitusRHI::AddressMode;

    // ------------------------------------------------------------------------
    // Format
    // ------------------------------------------------------------------------
    GLenum ToGLInternalFormat(Format format)
    {
        switch (format)
        {
        case Format::R8_UNORM:            return GL_R8;
        case Format::R8G8_UNORM:          return GL_RG8;
        case Format::R8G8B8_UNORM:        return GL_RGB8;
        case Format::R8G8B8A8_UNORM:      return GL_RGBA8;
        case Format::R8G8B8A8_SRGB:       return GL_SRGB8_ALPHA8;
        case Format::B8G8R8A8_UNORM:      return GL_RGBA8;
        case Format::B8G8R8A8_SRGB:       return GL_SRGB8_ALPHA8;
        case Format::R16_SFLOAT:          return GL_R16F;
        case Format::R16G16_SFLOAT:       return GL_RG16F;
        case Format::R16G16B16A16_SFLOAT: return GL_RGBA16F;
        case Format::R32_SFLOAT:          return GL_R32F;
        case Format::R32G32_SFLOAT:       return GL_RG32F;
        case Format::R32G32B32_SFLOAT:    return GL_RGB32F;
        case Format::R32G32B32A32_SFLOAT: return GL_RGBA32F;
        case Format::R32_UINT:            return GL_R32UI;
        case Format::R32_SINT:            return GL_R32I;
        case Format::D16_UNORM:           return GL_DEPTH_COMPONENT16;
        case Format::D24_UNORM_S8_UINT:   return GL_DEPTH24_STENCIL8;
        case Format::D32_SFLOAT:          return GL_DEPTH_COMPONENT32F;
        case Format::D32_SFLOAT_S8_UINT:  return GL_DEPTH32F_STENCIL8;
        default:                          return GL_RGBA8;
        }
    }

    GLenum ToGLDataFormat(Format format)
    {
        switch (format)
        {
        case Format::R8_UNORM:
        case Format::R16_SFLOAT:
        case Format::R32_SFLOAT:          return GL_RED;
        case Format::R32_UINT:            return GL_RED_INTEGER;
        case Format::R32_SINT:            return GL_RED_INTEGER;
        case Format::R8G8_UNORM:
        case Format::R16G16_SFLOAT:
        case Format::R32G32_SFLOAT:       return GL_RG;
        case Format::R8G8B8_UNORM:
        case Format::R32G32B32_SFLOAT:    return GL_RGB;
        case Format::R8G8B8A8_UNORM:
        case Format::R8G8B8A8_SRGB:
        case Format::R16G16B16A16_SFLOAT:
        case Format::R32G32B32A32_SFLOAT: return GL_RGBA;
        case Format::B8G8R8A8_UNORM:
        case Format::B8G8R8A8_SRGB:       return GL_BGRA;
        case Format::D16_UNORM:
        case Format::D32_SFLOAT:          return GL_DEPTH_COMPONENT;
        case Format::D24_UNORM_S8_UINT:
        case Format::D32_SFLOAT_S8_UINT:  return GL_DEPTH_STENCIL;
        default:                          return GL_RGBA;
        }
    }

    GLenum ToGLDataType(Format format)
    {
        switch (format)
        {
        case Format::R8_UNORM:
        case Format::R8G8_UNORM:
        case Format::R8G8B8_UNORM:
        case Format::R8G8B8A8_UNORM:
        case Format::R8G8B8A8_SRGB:
        case Format::B8G8R8A8_UNORM:
        case Format::B8G8R8A8_SRGB:       return GL_UNSIGNED_BYTE;
        case Format::R16_SFLOAT:
        case Format::R16G16_SFLOAT:
        case Format::R16G16B16A16_SFLOAT: return GL_HALF_FLOAT;
        case Format::R32_SFLOAT:
        case Format::R32G32_SFLOAT:
        case Format::R32G32B32_SFLOAT:
        case Format::R32G32B32A32_SFLOAT: return GL_FLOAT;
        case Format::R32_UINT:            return GL_UNSIGNED_INT;
        case Format::R32_SINT:            return GL_INT;
        case Format::D16_UNORM:           return GL_UNSIGNED_SHORT;
        case Format::D32_SFLOAT:          return GL_FLOAT;
        case Format::D24_UNORM_S8_UINT:   return GL_UNSIGNED_INT_24_8;
        case Format::D32_SFLOAT_S8_UINT:  return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
        default:                          return GL_UNSIGNED_BYTE;
        }
    }

    // ------------------------------------------------------------------------
    // 拓扑 / 索引
    // ------------------------------------------------------------------------
    GLenum ToGLPrimitive(PrimitiveTopology topology)
    {
        switch (topology)
        {
        case PrimitiveTopology::PointList:     return GL_POINTS;
        case PrimitiveTopology::LineList:      return GL_LINES;
        case PrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
        case PrimitiveTopology::TriangleList:  return GL_TRIANGLES;
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveTopology::TriangleFan:   return GL_TRIANGLE_FAN;
        }
        return GL_TRIANGLES;
    }

    GLenum ToGLIndexType(IndexType type)
    {
        return type == IndexType::UInt16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    }

    GLsizei IndexTypeBytes(IndexType type)
    {
        return type == IndexType::UInt16 ? 2 : 4;
    }

    // ------------------------------------------------------------------------
    // 比较 / 混合
    // ------------------------------------------------------------------------
    GLenum ToGLCompareOp(CompareOp op)
    {
        switch (op)
        {
        case CompareOp::Never:          return GL_NEVER;
        case CompareOp::Less:           return GL_LESS;
        case CompareOp::Equal:          return GL_EQUAL;
        case CompareOp::LessOrEqual:    return GL_LEQUAL;
        case CompareOp::Greater:        return GL_GREATER;
        case CompareOp::NotEqual:       return GL_NOTEQUAL;
        case CompareOp::GreaterOrEqual: return GL_GEQUAL;
        case CompareOp::Always:         return GL_ALWAYS;
        }
        return GL_LESS;
    }

    GLenum ToGLBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
        case BlendFactor::Zero:                  return GL_ZERO;
        case BlendFactor::One:                   return GL_ONE;
        case BlendFactor::SrcColor:              return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:      return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:              return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor:      return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:              return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:      return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:              return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:      return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor:         return GL_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
        }
        return GL_ZERO;
    }

    GLenum ToGLBlendOp(BlendOp op)
    {
        switch (op)
        {
        case BlendOp::Add:             return GL_FUNC_ADD;
        case BlendOp::Subtract:        return GL_FUNC_SUBTRACT;
        case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::Min:             return GL_MIN;
        case BlendOp::Max:             return GL_MAX;
        }
        return GL_FUNC_ADD;
    }

    // ------------------------------------------------------------------------
    // 光栅化
    // ------------------------------------------------------------------------
    GLenum ToGLCullMode(CullMode mode)
    {
        switch (mode)
        {
        case CullMode::None:         return 0;
        case CullMode::Front:        return GL_FRONT;
        case CullMode::Back:         return GL_BACK;
        case CullMode::FrontAndBack: return GL_FRONT_AND_BACK;
        }
        return GL_BACK;
    }

    GLenum ToGLFrontFace(FrontFace face)
    {
        return face == FrontFace::CounterClockwise ? GL_CCW : GL_CW;
    }

    GLenum ToGLPolygonMode(PolygonMode mode)
    {
        switch (mode)
        {
        case PolygonMode::Fill:  return GL_FILL;
        case PolygonMode::Line:  return GL_LINE;
        case PolygonMode::Point: return GL_POINT;
        }
        return GL_FILL;
    }

    // ------------------------------------------------------------------------
    // Shader 阶段
    // ------------------------------------------------------------------------
    GLenum ToGLShaderStage(ShaderStage stage)
    {
        switch (stage)
        {
        case ShaderStage::Vertex:         return GL_VERTEX_SHADER;
        case ShaderStage::Fragment:       return GL_FRAGMENT_SHADER;
        case ShaderStage::Geometry:       return GL_GEOMETRY_SHADER;
        case ShaderStage::Compute:        return GL_COMPUTE_SHADER;
        case ShaderStage::TessControl:    return GL_TESS_CONTROL_SHADER;
        case ShaderStage::TessEvaluation: return GL_TESS_EVALUATION_SHADER;
        default:                          return GL_VERTEX_SHADER;
        }
    }

    // ------------------------------------------------------------------------
    // 采样器
    // ------------------------------------------------------------------------
    GLenum ToGLMinFilter(FilterMode minFilter, MipmapMode mipMode, bool hasMipmap)
    {
        if (!hasMipmap) return minFilter == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
        if (minFilter == FilterMode::Nearest)
            return mipMode == MipmapMode::Nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_LINEAR;
        return mipMode == MipmapMode::Nearest ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
    }

    GLenum ToGLMagFilter(FilterMode magFilter)
    {
        return magFilter == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
    }

    GLenum ToGLAddressMode(AddressMode mode)
    {
        switch (mode)
        {
        case AddressMode::Repeat:         return GL_REPEAT;
        case AddressMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:    return GL_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder:  return GL_CLAMP_TO_BORDER;
        }
        return GL_REPEAT;
    }
}
