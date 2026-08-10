#pragma once
// ============================================================================
// RendererCore - GHandle
// 不透明、类型安全的 GPU 资源句柄；上层只持有 Handle，不接触任何后端原生类型。
// ============================================================================
#include <cstdint>

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // 模板句柄：内部仅一个 uint64_t id，是 POD；通过 Tag 提供编译期类型安全。
    // 不同 Tag 的句柄互相赋值/比较时编译器会报错。
    // ------------------------------------------------------------------------
    template <typename Tag>
    struct GHandle
    {
        uint64_t id = 0;

        constexpr GHandle() = default;

        constexpr explicit GHandle(uint64_t value) : id(value)
        {
        }

        // 是否合法（约定 0 为非法句柄）
        constexpr bool IsValid() const { return id != 0; }

        constexpr bool operator==(GHandle other) const { return id == other.id; }
        constexpr bool operator!=(GHandle other) const { return id != other.id; }
    };

    // ------------------------------------------------------------------------
    // 资源类型 Tag —— 仅用于模板特化，无任何成员。
    // ------------------------------------------------------------------------
    struct BufferTag
    {
    };

    struct TextureTag
    {
    };

    struct SamplerTag
    {
    };

    struct ShaderTag
    {
    };

    struct PipelineTag
    {
    };

    struct RenderTargetTag
    {
    };

    // 光追：加速结构句柄 Tag
    struct AccelerationStructureTag
    {
    };

    // ------------------------------------------------------------------------
    // 核心资源句柄类型别名
    // ------------------------------------------------------------------------
    using BufferHandle = GHandle<BufferTag>;
    using TextureHandle = GHandle<TextureTag>;
    using SamplerHandle = GHandle<SamplerTag>;
    using ShaderHandle = GHandle<ShaderTag>;
    using PipelineHandle = GHandle<PipelineTag>;
    using RenderTargetHandle = GHandle<RenderTargetTag>;
    // 光追：加速结构句柄
    using AccelerationStructureHandle = GHandle<AccelerationStructureTag>;
}
