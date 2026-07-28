#pragma once
// ============================================================================
// RendererVK - VKTranslate
// 集中实现 RendererCore 后端无关枚举到 Vulkan 原生类型的翻译。
// 任何向 Vulkan 翻译的逻辑都应集中在这里，避免分散到多个文件造成不一致。
// ============================================================================
#include <vulkan/vulkan.h>

#include "GDescs.h"
#include "RendererCore/GEnums.h"

namespace TitusVkGraphics
{
    // ------------------------------------------------------------------------
    // Format ↔ VkFormat
    // ------------------------------------------------------------------------
    VkFormat                 ToVkFormat(TitusRHI::Format format);
    TitusRHI::Format       FromVkFormat(VkFormat format);

    // ------------------------------------------------------------------------
    // 拓扑 / 索引类型
    // ------------------------------------------------------------------------
    VkPrimitiveTopology      ToVkTopology(TitusRHI::PrimitiveTopology topology);
    VkIndexType              ToVkIndexType(TitusRHI::IndexType type);

    // ------------------------------------------------------------------------
    // 光栅化
    // ------------------------------------------------------------------------
    VkCullModeFlags          ToVkCullMode(TitusRHI::CullMode mode);
    VkFrontFace              ToVkFrontFace(TitusRHI::FrontFace face);
    VkPolygonMode            ToVkPolygonMode(TitusRHI::PolygonMode mode);

    // ------------------------------------------------------------------------
    // 比较 / 混合
    // ------------------------------------------------------------------------
    VkCompareOp              ToVkCompareOp(TitusRHI::CompareOp op);
    VkBlendFactor            ToVkBlendFactor(TitusRHI::BlendFactor factor);
    VkBlendOp                ToVkBlendOp(TitusRHI::BlendOp op);

    // ------------------------------------------------------------------------
    // 附件 LoadOp / StoreOp
    // ------------------------------------------------------------------------
    VkAttachmentLoadOp       ToVkLoadOp(TitusRHI::LoadOp op);
    VkAttachmentStoreOp      ToVkStoreOp(TitusRHI::StoreOp op);

    // ------------------------------------------------------------------------
    // 着色器阶段
    // ------------------------------------------------------------------------
    VkShaderStageFlagBits    ToVkShaderStage(TitusRHI::ShaderStage stage);
    VkShaderStageFlags       ToVkShaderStageFlags(TitusRHI::ShaderStage stages);

    // ------------------------------------------------------------------------
    // Buffer / Texture / Memory
    // ------------------------------------------------------------------------
    VkBufferUsageFlags       ToVkBufferUsage(TitusRHI::BufferUsage usage);
    VkImageUsageFlags        ToVkImageUsage(TitusRHI::TextureUsage usage);
    VkMemoryPropertyFlags    ToVkMemoryProps(TitusRHI::MemoryUsage usage);

    // ------------------------------------------------------------------------
    // 采样器
    // ------------------------------------------------------------------------
    VkFilter                 ToVkFilter(TitusRHI::FilterMode mode);
    VkSamplerMipmapMode      ToVkMipmapMode(TitusRHI::MipmapMode mode);
    VkSamplerAddressMode     ToVkAddressMode(TitusRHI::AddressMode mode);

    // ------------------------------------------------------------------------
    // 资源绑定
    // ------------------------------------------------------------------------
    VkDescriptorType ToVkDescriptorType(TitusRHI::ResourceBindingType type);
}
