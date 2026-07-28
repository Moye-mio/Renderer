#pragma once
// ============================================================================
// RendererCore - GStateCache
// 状态对象哈希去重缓存（任务 8）：相同 SamplerDesc / GraphicsPipelineDesc 只
// 创建一次，后续请求复用已有句柄，降低无谓的 GPU 状态对象创建（特别是 Vulkan
// VkPipeline / VkSampler，创建开销很高）。
//
// 设计参考：requirements.md 需求 16.4（RasterState/DepthState/BlendState/
// SamplerState 等状态对象的缓存复用）。
//
// 注意：
//   - SamplerDesc 仅含 trivially-copyable 字段（debugName 为 const char*，按指针
//     比较即可），可直接 memcmp 哈希。
//   - GraphicsPipelineDesc 含 std::vector<VertexBinding/Attribute/Blend/...>
//     这类非 POD 字段，使用基于字段语义的 hash + equals。
// ============================================================================
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>

#include "GHandle.h"
#include "GDescs.h"

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // 通用工具：FNV-1a 64 字节哈希
    // ------------------------------------------------------------------------
    inline size_t HashBytesFNV1a(const void* data, size_t bytes, size_t seed = 1469598103934665603ull)
    {
        const auto* p = reinterpret_cast<const uint8_t*>(data);
        size_t h = seed;
        for (size_t i = 0; i < bytes; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    template<typename T>
    inline size_t HashOfPod(const T& v, size_t seed = 1469598103934665603ull)
    {
        static_assert(std::is_trivially_copyable<T>::value, "HashOfPod requires trivially copyable");
        return HashBytesFNV1a(&v, sizeof(T), seed);
    }

    // ------------------------------------------------------------------------
    // SamplerDesc 缓存（trivially-copyable，可直接按字节比较）
    // ------------------------------------------------------------------------
    struct SamplerDescHash
    {
        size_t operator()(const SamplerDesc& d) const noexcept { return HashOfPod(d); }
    };
    struct SamplerDescEq
    {
        bool operator()(const SamplerDesc& a, const SamplerDesc& b) const noexcept
        {
            return std::memcmp(&a, &b, sizeof(SamplerDesc)) == 0;
        }
    };
    using SamplerCache = std::unordered_map<SamplerDesc, SamplerHandle,
                                            SamplerDescHash, SamplerDescEq>;

    // ------------------------------------------------------------------------
    // GraphicsPipelineDesc 缓存（含 std::vector 字段）
    // ------------------------------------------------------------------------
    struct PipelineDescHash
    {
        size_t operator()(const GraphicsPipelineDesc& d) const noexcept
        {
            size_t h = 1469598103934665603ull;
            h = HashOfPod(d.vertexShader.id, h);
            h = HashOfPod(d.fragmentShader.id, h);
            h = HashOfPod(d.geometryShader.id, h);
            h = HashOfPod(d.topology, h);
            h = HashOfPod(d.rasterizer, h);
            h = HashOfPod(d.depthStencil, h);

            for (const auto& b : d.vertexLayout.bindings)   h = HashOfPod(b, h);
            for (const auto& a : d.vertexLayout.attributes) h = HashOfPod(a, h);
            for (const auto& a : d.blend.attachments)       h = HashOfPod(a, h);
            for (const auto& f : d.rtLayout.colorFormats)   h = HashOfPod(f, h);
            h = HashOfPod(d.rtLayout.depthStencilFormat, h);
            h = HashOfPod(d.rtLayout.samples, h);
            return h;
        }
    };

    struct PipelineDescEq
    {
        bool operator()(const GraphicsPipelineDesc& a, const GraphicsPipelineDesc& b) const noexcept
        {
            if (a.vertexShader.id   != b.vertexShader.id)   return false;
            if (a.fragmentShader.id != b.fragmentShader.id) return false;
            if (a.geometryShader.id != b.geometryShader.id) return false;
            if (a.topology != b.topology) return false;
            if (std::memcmp(&a.rasterizer,   &b.rasterizer,   sizeof(RasterizerState))   != 0) return false;
            if (std::memcmp(&a.depthStencil, &b.depthStencil, sizeof(DepthStencilState)) != 0) return false;

            if (a.vertexLayout.bindings.size()   != b.vertexLayout.bindings.size())   return false;
            if (a.vertexLayout.attributes.size() != b.vertexLayout.attributes.size()) return false;
            if (a.blend.attachments.size()       != b.blend.attachments.size())       return false;
            if (a.rtLayout.colorFormats.size()   != b.rtLayout.colorFormats.size())   return false;

            if (!a.vertexLayout.bindings.empty()
             && std::memcmp(a.vertexLayout.bindings.data(), b.vertexLayout.bindings.data(),
                            sizeof(VertexBinding) * a.vertexLayout.bindings.size()) != 0) return false;
            if (!a.vertexLayout.attributes.empty()
             && std::memcmp(a.vertexLayout.attributes.data(), b.vertexLayout.attributes.data(),
                            sizeof(VertexAttribute) * a.vertexLayout.attributes.size()) != 0) return false;
            if (!a.blend.attachments.empty()
             && std::memcmp(a.blend.attachments.data(), b.blend.attachments.data(),
                            sizeof(BlendAttachmentState) * a.blend.attachments.size()) != 0) return false;
            if (!a.rtLayout.colorFormats.empty()
             && std::memcmp(a.rtLayout.colorFormats.data(), b.rtLayout.colorFormats.data(),
                            sizeof(Format) * a.rtLayout.colorFormats.size()) != 0) return false;
            if (a.rtLayout.depthStencilFormat != b.rtLayout.depthStencilFormat) return false;
            if (a.rtLayout.samples            != b.rtLayout.samples)            return false;
            return true;
        }
    };

    using PipelineCache = std::unordered_map<GraphicsPipelineDesc, PipelineHandle,
                                             PipelineDescHash, PipelineDescEq>;
}
