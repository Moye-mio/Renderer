// ============================================================================
// RendererVK - VKTranslate.cpp
// 翻译表：把后端无关枚举一次性映射成 Vulkan 原生类型。
// ============================================================================
#include "VKTranslate.h"

namespace TitusVkGraphics
{
    using TitusRHI::Format;
    using TitusRHI::PrimitiveTopology;
    using TitusRHI::IndexType;
    using TitusRHI::CullMode;
    using TitusRHI::FrontFace;
    using TitusRHI::PolygonMode;
    using TitusRHI::CompareOp;
    using TitusRHI::BlendFactor;
    using TitusRHI::BlendOp;
    using TitusRHI::LoadOp;
    using TitusRHI::StoreOp;
    using TitusRHI::ShaderStage;
    using TitusRHI::BufferUsage;
    using TitusRHI::TextureUsage;
    using TitusRHI::MemoryUsage;
    using TitusRHI::FilterMode;
    using TitusRHI::MipmapMode;
    using TitusRHI::AddressMode;
    using TitusRHI::ResourceBindingType;

    // ------------------------------------------------------------------------
    // Format
    // ------------------------------------------------------------------------
    VkFormat ToVkFormat(Format format)
    {
        switch (format)
        {
        case Format::Undefined:           return VK_FORMAT_UNDEFINED;
        case Format::R8_UNORM:            return VK_FORMAT_R8_UNORM;
        case Format::R8G8_UNORM:          return VK_FORMAT_R8G8_UNORM;
        case Format::R8G8B8_UNORM:        return VK_FORMAT_R8G8B8_UNORM;
        case Format::R8G8B8A8_UNORM:      return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::R8G8B8A8_SRGB:       return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::B8G8R8A8_UNORM:      return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::B8G8R8A8_SRGB:       return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::R16_SFLOAT:          return VK_FORMAT_R16_SFLOAT;
        case Format::R16G16_SFLOAT:       return VK_FORMAT_R16G16_SFLOAT;
        case Format::R16G16B16A16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::R32_SFLOAT:          return VK_FORMAT_R32_SFLOAT;
        case Format::R32G32_SFLOAT:       return VK_FORMAT_R32G32_SFLOAT;
        case Format::R32G32B32_SFLOAT:    return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::R32G32B32A32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::R32_UINT:            return VK_FORMAT_R32_UINT;
        case Format::R32_SINT:            return VK_FORMAT_R32_SINT;
        case Format::D16_UNORM:           return VK_FORMAT_D16_UNORM;
        case Format::D24_UNORM_S8_UINT:   return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32_SFLOAT:          return VK_FORMAT_D32_SFLOAT;
        case Format::D32_SFLOAT_S8_UINT:  return VK_FORMAT_D32_SFLOAT_S8_UINT;
        }
        return VK_FORMAT_UNDEFINED;
    }

    Format FromVkFormat(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_R8_UNORM:            return Format::R8_UNORM;
        case VK_FORMAT_R8G8_UNORM:          return Format::R8G8_UNORM;
        case VK_FORMAT_R8G8B8_UNORM:        return Format::R8G8B8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM:      return Format::R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB:       return Format::R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:      return Format::B8G8R8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB:       return Format::B8G8R8A8_SRGB;
        case VK_FORMAT_R16_SFLOAT:          return Format::R16_SFLOAT;
        case VK_FORMAT_R16G16_SFLOAT:       return Format::R16G16_SFLOAT;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::R16G16B16A16_SFLOAT;
        case VK_FORMAT_R32_SFLOAT:          return Format::R32_SFLOAT;
        case VK_FORMAT_R32G32_SFLOAT:       return Format::R32G32_SFLOAT;
        case VK_FORMAT_R32G32B32_SFLOAT:    return Format::R32G32B32_SFLOAT;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::R32G32B32A32_SFLOAT;
        case VK_FORMAT_R32_UINT:            return Format::R32_UINT;
        case VK_FORMAT_R32_SINT:            return Format::R32_SINT;
        case VK_FORMAT_D16_UNORM:           return Format::D16_UNORM;
        case VK_FORMAT_D24_UNORM_S8_UINT:   return Format::D24_UNORM_S8_UINT;
        case VK_FORMAT_D32_SFLOAT:          return Format::D32_SFLOAT;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:  return Format::D32_SFLOAT_S8_UINT;
        default:                            return Format::Undefined;
        }
    }

    // ------------------------------------------------------------------------
    // 拓扑 / 索引类型
    // ------------------------------------------------------------------------
    VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology)
    {
        switch (topology)
        {
        case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopology::TriangleFan:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        }
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    VkIndexType ToVkIndexType(IndexType type)
    {
        return type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    }

    // ------------------------------------------------------------------------
    // 光栅化
    // ------------------------------------------------------------------------
    VkCullModeFlags ToVkCullMode(CullMode mode)
    {
        switch (mode)
        {
        case CullMode::None:         return VK_CULL_MODE_NONE;
        case CullMode::Front:        return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:         return VK_CULL_MODE_BACK_BIT;
        case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
        }
        return VK_CULL_MODE_BACK_BIT;
    }

    VkFrontFace ToVkFrontFace(FrontFace face)
    {
        return face == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                   : VK_FRONT_FACE_CLOCKWISE;
    }

    VkPolygonMode ToVkPolygonMode(PolygonMode mode)
    {
        switch (mode)
        {
        case PolygonMode::Fill:  return VK_POLYGON_MODE_FILL;
        case PolygonMode::Line:  return VK_POLYGON_MODE_LINE;
        case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
        }
        return VK_POLYGON_MODE_FILL;
    }

    // ------------------------------------------------------------------------
    // 比较 / 混合
    // ------------------------------------------------------------------------
    VkCompareOp ToVkCompareOp(CompareOp op)
    {
        switch (op)
        {
        case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:           return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
        }
        return VK_COMPARE_OP_LESS;
    }

    VkBlendFactor ToVkBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
        case BlendFactor::Zero:                  return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:                   return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:              return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:              return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:              return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:              return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor:         return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        }
        return VK_BLEND_FACTOR_ZERO;
    }

    VkBlendOp ToVkBlendOp(BlendOp op)
    {
        switch (op)
        {
        case BlendOp::Add:             return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:             return VK_BLEND_OP_MIN;
        case BlendOp::Max:             return VK_BLEND_OP_MAX;
        }
        return VK_BLEND_OP_ADD;
    }

    // ------------------------------------------------------------------------
    // 附件
    // ------------------------------------------------------------------------
    VkAttachmentLoadOp ToVkLoadOp(LoadOp op)
    {
        switch (op)
        {
        case LoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    VkAttachmentStoreOp ToVkStoreOp(StoreOp op)
    {
        switch (op)
        {
        case StoreOp::Store:    return VK_ATTACHMENT_STORE_OP_STORE;
        case StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    // ------------------------------------------------------------------------
    // 着色器阶段
    // ------------------------------------------------------------------------
    VkShaderStageFlagBits ToVkShaderStage(ShaderStage stage)
    {
        switch (stage)
        {
        case ShaderStage::Vertex:         return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::Fragment:       return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Geometry:       return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::Compute:        return VK_SHADER_STAGE_COMPUTE_BIT;
        case ShaderStage::TessControl:    return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStage::TessEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追管线阶段（任务 12 / 需求 10.3，P1）
        case ShaderStage::RayGen:         return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        case ShaderStage::Miss:           return VK_SHADER_STAGE_MISS_BIT_KHR;
        case ShaderStage::ClosestHit:     return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        case ShaderStage::AnyHit:         return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        case ShaderStage::Intersection:   return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        case ShaderStage::Callable:       return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
#endif
        default:                          return VK_SHADER_STAGE_VERTEX_BIT;
        }
    }

    VkShaderStageFlags ToVkShaderStageFlags(ShaderStage stages)
    {
        VkShaderStageFlags flags = 0;
        const uint32_t v = static_cast<uint32_t>(stages);
        if (v & static_cast<uint32_t>(ShaderStage::Vertex))         flags |= VK_SHADER_STAGE_VERTEX_BIT;
        if (v & static_cast<uint32_t>(ShaderStage::Fragment))       flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (v & static_cast<uint32_t>(ShaderStage::Geometry))       flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
        if (v & static_cast<uint32_t>(ShaderStage::Compute))        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
        if (v & static_cast<uint32_t>(ShaderStage::TessControl))    flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        if (v & static_cast<uint32_t>(ShaderStage::TessEvaluation)) flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追管线阶段（任务 12 / 需求 10.3，P1）
        if (v & static_cast<uint32_t>(ShaderStage::RayGen))         flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        if (v & static_cast<uint32_t>(ShaderStage::Miss))           flags |= VK_SHADER_STAGE_MISS_BIT_KHR;
        if (v & static_cast<uint32_t>(ShaderStage::ClosestHit))     flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        if (v & static_cast<uint32_t>(ShaderStage::AnyHit))         flags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        if (v & static_cast<uint32_t>(ShaderStage::Intersection))   flags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        if (v & static_cast<uint32_t>(ShaderStage::Callable))       flags |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
#endif
        return flags;
    }

    // ------------------------------------------------------------------------
    // Buffer / Texture / Memory
    // ------------------------------------------------------------------------
    VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage)
    {
        VkBufferUsageFlags flags = 0;
        const uint32_t v = static_cast<uint32_t>(usage);
        if (v & static_cast<uint32_t>(BufferUsage::VertexBuffer))  flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (v & static_cast<uint32_t>(BufferUsage::IndexBuffer))   flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (v & static_cast<uint32_t>(BufferUsage::UniformBuffer)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (v & static_cast<uint32_t>(BufferUsage::StorageBuffer)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (v & static_cast<uint32_t>(BufferUsage::TransferSrc))   flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (v & static_cast<uint32_t>(BufferUsage::TransferDst))   flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (v & static_cast<uint32_t>(BufferUsage::Indirect))      flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追（任务 7 / 需求 3.2）：新增 buffer usage 映射为对应 KHR 位。
        if (v & static_cast<uint32_t>(BufferUsage::ShaderDeviceAddress))
            flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if (v & static_cast<uint32_t>(BufferUsage::AccelerationStructureStorage))
            flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        if (v & static_cast<uint32_t>(BufferUsage::AccelerationStructureBuildInput))
            flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        if (v & static_cast<uint32_t>(BufferUsage::ShaderBindingTable))
            flags |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
#endif
        return flags;
    }

    VkImageUsageFlags ToVkImageUsage(TextureUsage usage)
    {
        VkImageUsageFlags flags = 0;
        const uint32_t v = static_cast<uint32_t>(usage);
        if (v & static_cast<uint32_t>(TextureUsage::Sampled))                flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (v & static_cast<uint32_t>(TextureUsage::Storage))                flags |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (v & static_cast<uint32_t>(TextureUsage::ColorAttachment))        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (v & static_cast<uint32_t>(TextureUsage::DepthStencilAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (v & static_cast<uint32_t>(TextureUsage::TransferSrc))            flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (v & static_cast<uint32_t>(TextureUsage::TransferDst))            flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        return flags;
    }

    VkMemoryPropertyFlags ToVkMemoryProps(MemoryUsage usage)
    {
        switch (usage)
        {
        case MemoryUsage::GpuOnly:  return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case MemoryUsage::CpuToGpu: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case MemoryUsage::GpuToCpu: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        case MemoryUsage::CpuOnly:  return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    // ------------------------------------------------------------------------
    // 采样器
    // ------------------------------------------------------------------------
    VkFilter ToVkFilter(FilterMode mode)
    {
        return mode == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    }

    VkSamplerMipmapMode ToVkMipmapMode(MipmapMode mode)
    {
        return mode == MipmapMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    VkSamplerAddressMode ToVkAddressMode(AddressMode mode)
    {
        switch (mode)
        {
        case AddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        }
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    // ------------------------------------------------------------------------
    // 资源绑定
    // ------------------------------------------------------------------------
    VkDescriptorType ToVkDescriptorType(ResourceBindingType type)
    {
        switch (type)
        {
        case ResourceBindingType::UniformBuffer:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case ResourceBindingType::StorageBuffer:        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case ResourceBindingType::SampledTexture:       return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case ResourceBindingType::StorageTexture:       return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case ResourceBindingType::Sampler:              return VK_DESCRIPTOR_TYPE_SAMPLER;
        case ResourceBindingType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追（任务 7 / 需求 8.1）：TLAS 描述符类型。
        case ResourceBindingType::AccelerationStructure: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
#endif
        }
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}
