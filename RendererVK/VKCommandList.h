#pragma once
// ============================================================================
// RendererVK - VKCommandList
// RenderCommandList 的 Vulkan 实现：
//   - 内部持有一个 VkCommandBuffer（每帧由 VKDevice 提供）
//   - 把每条 RendererCore 命令翻译为对应的 vkCmdXxx
//   - BeginRenderPass 翻译为 vkCmdBeginRenderPass（按 LoadOp/StoreOp 翻译附件操作）
// ============================================================================
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <unordered_map>
#include <cstdint>

#include "RendererCore/RenderCommandList.h"
#include "RendererCore/GHandle.h"

namespace TitusVkGraphics
{
    class VKDevice;

    // 每帧 BindResourceSet 路径计数（供 Overlay / 性能排查）
    struct DescriptorBindStats
    {
        uint32_t bindResourceSetCalls = 0;
        uint32_t descriptorAllocs     = 0;
        uint32_t descriptorCacheHits  = 0;
    };

    class VKCommandList : public TitusRHI::RenderCommandList
    {
    public:
        explicit VKCommandList(VKDevice* device);
        ~VKCommandList() override = default;

        // 由 VKDevice 在每帧的 BeginFrame 中调用，重置内部状态与本帧计数
        void Reset(VkCommandBuffer cmd);

        // 由 VKDevice 在 Submit 末尾调用：把本帧计数固化，供下一帧 Overlay 读取
        // （Overlay 在 DrawFrame 之前绘制，不能依赖 BeginFrame/Reset 时机）
        void PublishFrameStats();

        // 上一完整帧的 BindResourceSet 统计（Submit 时固化）
        const DescriptorBindStats& GetLastFrameDescriptorBindStats() const
        {
            return m_lastFrameStats;
        }

        // ====================================================================
        // RenderCommandList 实现
        // ====================================================================
        void BeginRenderPass(const TitusRHI::RenderPassBeginInfo& info) override;
        void EndRenderPass() override;

        void SetViewport(const TitusRHI::Viewport& viewport) override;
        void SetScissor (const TitusRHI::Rect2D&  scissor)   override;

        void BindPipeline    (TitusRHI::PipelineHandle pipeline) override;
        void BindVertexBuffer(uint32_t                   slot,
                              TitusRHI::BufferHandle   buffer,
                              uint64_t                   offset = 0) override;
        void BindIndexBuffer (TitusRHI::BufferHandle   buffer,
                              TitusRHI::IndexType      indexType,
                              uint64_t                   offset = 0) override;

        void BindResourceSet (uint32_t                          setIndex,
                              const TitusRHI::ResourceSetDesc& setDesc) override;

        void PushConstants   (TitusRHI::ShaderStage stages,
                              uint32_t                offset,
                              uint32_t                size,
                              const void*             data) override;

        void Draw       (uint32_t vertexCount,
                         uint32_t instanceCount = 1,
                         uint32_t firstVertex   = 0,
                         uint32_t firstInstance = 0) override;
        void DrawIndexed(uint32_t indexCount,
                         uint32_t instanceCount = 1,
                         uint32_t firstIndex    = 0,
                         int32_t  vertexOffset  = 0,
                         uint32_t firstInstance = 0) override;

        // ====================================================================
        // Compute（任务 7v-1）
        // ====================================================================
        void Dispatch(uint32_t groupCountX,
                      uint32_t groupCountY,
                      uint32_t groupCountZ) override;

        void PipelineBarrier(const TitusRHI::PipelineBarrierDesc& desc) override;

#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追派发（P1，任务 15）：vkCmdTraceRaysKHR
        void TraceRays(uint32_t width, uint32_t height, uint32_t depth) override;
        // 加速结构构建/refit（P2，任务 16）：命令流内 vkCmdBuildAccelerationStructuresKHR
        void BuildAccelerationStructure(TitusRHI::AccelerationStructureHandle target,
                                        const TitusRHI::AccelerationStructureBuildInfo& info) override;
#endif

        VkCommandBuffer GetVkCommandBuffer() const { return m_cmd; }

    private:
        // 帧内 DS 内容 key：layout + 排序后的 binding 槽位
        struct DsContentKey
        {
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            struct Slot
            {
                uint32_t binding = 0;
                uint32_t type    = 0;
                uint64_t id0     = 0; // buffer / texture / sampler / AS
                uint64_t id1     = 0; // sampler (CIS) / unused
                uint64_t offset  = 0;
                uint64_t range   = 0;

                bool operator==(const Slot& o) const
                {
                    return binding == o.binding && type == o.type
                        && id0 == o.id0 && id1 == o.id1
                        && offset == o.offset && range == o.range;
                }
            };
            std::vector<Slot> slots;

            bool operator==(const DsContentKey& o) const
            {
                return layout == o.layout && slots == o.slots;
            }
        };

        struct DsContentKeyHash
        {
            size_t operator()(const DsContentKey& k) const noexcept
            {
                size_t h = static_cast<size_t>(reinterpret_cast<uintptr_t>(k.layout));
                auto mix = [&](uint64_t v)
                {
                    h ^= static_cast<size_t>(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                };
                for (const auto& s : k.slots)
                {
                    mix(s.binding);
                    mix(s.type);
                    mix(s.id0);
                    mix(s.id1);
                    mix(s.offset);
                    mix(s.range);
                }
                return h;
            }
        };

        static DsContentKey MakeContentKey(VkDescriptorSetLayout layout,
                                           const TitusRHI::ResourceSetDesc& desc);

        VKDevice*                m_device = nullptr;
        VkCommandBuffer          m_cmd    = VK_NULL_HANDLE;

        // 当前绑定的 Pipeline 信息（用于 BindResourceSet/PushConstants 找 layout）
        VkPipeline               m_currentPipeline = VK_NULL_HANDLE;
        VkPipelineLayout         m_currentLayout   = VK_NULL_HANDLE;
        // 任务 7v-1：BindPipeline 时记录 bindPoint，供 Dispatch / 后续 BindDescriptorSets 使用
        VkPipelineBindPoint      m_currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        // 任务 7v-2：保留指向 VKPipelineEntry 的弱引用，BindResourceSet 据 setLayouts[setIndex]
        // 分配 DescriptorSet。该指针仅在同一帧内、Pipeline 未销毁时有效。
        const struct VKPipelineEntry* m_currentPipelineEntry = nullptr;

        // 任务 11：BeginRenderPass 时记录当前活跃的 RenderTarget 句柄；
        // EndRenderPass 时把所有 color attachment 的 currentLayout 同步为
        // SHADER_READ_ONLY_OPTIMAL（与 RT.RenderPass 中 finalLayout 一致），
        // 这样后续 Pass 的 BindResourceSet 能用正确 layout 走 sampled image 路径。
        TitusRHI::RenderTargetHandle m_activeRenderTarget;

        // 任务 11.x：per-set ResourceSetDesc 状态缓存（GL 风格增量绑定兼容层）
        //
        // 业务侧（如 DrawGpuModelWithDiffuse）的设计假设是 GL 风格：
        //   外层先 BindResourceSet(0, {UBO@binding=0});
        //   循环每个 SubMesh: BindResourceSet(0, {Diffuse@binding=1}); // 只写 binding=1
        // GL 端 binding 是全局状态机，前后两次绑互不干扰；VK 端需要模拟这个行为。
        //
        // 修复策略：维护 per-set 的完整 ResourceSetDesc 状态，每次 BindResourceSet
        // 时把新 binding 合并进去，然后按完整内容查帧内 DS 缓存（命中则复用，
        // 未命中再 Allocate + Update）。这样既避免更新已绑定 DS，又保留增量语义。
        static constexpr uint32_t kMaxCachedSets = 8;
        std::array<TitusRHI::ResourceSetDesc, kMaxCachedSets> m_setStateCache{};
        // 当前 cmd 上已绑定的 DS（同 setIndex 相同 DS 时跳过 vkCmdBindDescriptorSets）
        std::array<VkDescriptorSet, kMaxCachedSets> m_boundSets{};
        // 帧内按 (layout + 完整 binding 内容) 复用已分配的 DS
        std::unordered_map<DsContentKey, VkDescriptorSet, DsContentKeyHash> m_dsContentCache;

        DescriptorBindStats m_frameStats{};
        DescriptorBindStats m_lastFrameStats{};
    };
}
