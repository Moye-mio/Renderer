#pragma once
// ============================================================================
// RendererCore - RayTracingManager
// 加速结构管理层：集中管理动态场景的加速结构实例。
// 作为建立在 AS 基础设施（IGDevice::CreateAccelerationStructure /
// Destroy、RenderCommandList::BuildAccelerationStructure）之上的**独立层**，
// 集中处理动态场景的：
//   - instance 列表的增删改
//   - 多实例引用同一几何时的 BLAS 复用/去重
//   - TLAS 的重建（拓扑变化）与 refit（仅 transform 变化，依赖 AllowUpdate）
// 本层不修改、不破坏已冻结的 RendererCore 光追接口；仅使用其公开抽象。
// 全部字段仅用 RendererCore 自定义句柄/枚举/POD，禁止出现任何 VkXxx。
// ============================================================================
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include "GHandle.h"
#include "GDescs.h"

namespace TitusRHI
{
    class IGDevice;
    class RenderCommandList;

    class RayTracingManager
    {
    public:
        using InstanceID = uint32_t;
        static constexpr InstanceID kInvalidInstance = ~0u;

        explicit RayTracingManager(IGDevice& device);
        ~RayTracingManager();

        RayTracingManager(const RayTracingManager&) = delete;
        RayTracingManager& operator=(const RayTracingManager&) = delete;

        // BLAS 复用/去重：相同几何（同 vertex/index buffer + 数量）
        // 返回同一 BLAS，避免重复构建。首次调用时创建。
        AccelerationStructureHandle GetOrCreateBLAS(const BLASGeometryDesc& geom,
                                                    ASBuildFlags flags = ASBuildFlags::PreferFastTrace);

        // instance 管理。返回稳定的 InstanceID（不随其他实例增删变化）。
        InstanceID AddInstance(const TLASInstanceDesc& inst);
        void       RemoveInstance(InstanceID id);
        void       SetInstance(InstanceID id, const TLASInstanceDesc& inst);
        void       SetInstanceTransform(InstanceID id, const float transform[12]);

        // 构建/刷新 TLAS。
        //   - 首次、或 instance 拓扑发生增删、或未提供 cmd → 全量重建
        //     （Destroy 旧 TLAS 走延迟销毁 + CreateAccelerationStructure 新建）；
        //   - 仅 transform 变化且提供了 cmd 且 TLAS 支持 AllowUpdate →
        //     经 cmd->BuildAccelerationStructure 做 refit（增量更新）。
        // 返回当前 TLAS 句柄（可能因重建而变化）。
        AccelerationStructureHandle BuildOrRefit(RenderCommandList* cmd = nullptr);

        AccelerationStructureHandle GetTLAS() const { return m_tlas; }
        uint32_t GetLiveInstanceCount() const { return m_liveCount; }

        // 释放所有 BLAS/TLAS 与内部状态。
        void Clear();

    private:
        std::vector<TLASInstanceDesc> CollectLiveInstances() const;
        void RebuildTLAS();

        IGDevice& m_device;

        // BLAS 去重表：key = (vertexBuffer.id, indexBuffer.id, vertexCount, indexCount)
        using BlasKey = std::tuple<uint64_t, uint64_t, uint32_t, uint32_t>;
        std::map<BlasKey, AccelerationStructureHandle> m_blasCache;

        // instance 槽（RemoveInstance 置 alive=false，槽位可被后续 AddInstance 复用）
        std::vector<TLASInstanceDesc> m_instances;
        std::vector<bool>             m_alive;
        std::vector<InstanceID>       m_freeList;
        uint32_t                      m_liveCount = 0;

        AccelerationStructureHandle   m_tlas;
        uint32_t                      m_tlasInstanceCount = 0; // 上次构建时的存活 instance 数

        bool m_topologyDirty  = false; // 增删导致的拓扑变化 → 需重建
        bool m_transformDirty = false; // 仅 transform/属性变化 → 可 refit
    };
}
