// ============================================================================
// RendererCore - RayTracingManager.cpp（P2，任务 16）
// 仅依赖 IGDevice / RenderCommandList 抽象接口。
// ============================================================================
#include "RayTracingManager.h"

#include "IGDevice.h"
#include "RenderCommandList.h"
#include "Logger.h"

namespace TitusRHI
{
    RayTracingManager::RayTracingManager(IGDevice& device)
        : m_device(device)
    {
    }

    RayTracingManager::~RayTracingManager()
    {
        Clear();
    }

    AccelerationStructureHandle RayTracingManager::GetOrCreateBLAS(
        const BLASGeometryDesc& geom, ASBuildFlags flags)
    {
        const BlasKey key{ geom.vertexBuffer.id, geom.indexBuffer.id, geom.vertexCount, geom.indexCount };
        auto it = m_blasCache.find(key);
        if (it != m_blasCache.end() && it->second.IsValid())
            return it->second; // 复用/去重（需求 15.2）

        AccelerationStructureDesc desc{};
        desc.type = AccelerationStructureType::BottomLevel;
        desc.buildFlags = flags;
        desc.geometries.push_back(geom);
        desc.debugName = "RTASManager.BLAS";

        AccelerationStructureHandle blas = m_device.CreateAccelerationStructure(desc);
        if (blas.IsValid())
            m_blasCache[key] = blas;
        return blas;
    }

    RayTracingManager::InstanceID
    RayTracingManager::AddInstance(const TLASInstanceDesc& inst)
    {
        InstanceID id;
        if (!m_freeList.empty())
        {
            id = m_freeList.back();
            m_freeList.pop_back();
            m_instances[id] = inst;
            m_alive[id] = true;
        }
        else
        {
            id = static_cast<InstanceID>(m_instances.size());
            m_instances.push_back(inst);
            m_alive.push_back(true);
        }
        ++m_liveCount;
        m_topologyDirty = true; // 新增 instance → 拓扑变化，需重建
        return id;
    }

    void RayTracingManager::RemoveInstance(InstanceID id)
    {
        if (id >= m_instances.size() || !m_alive[id]) return;
        m_alive[id] = false;
        m_freeList.push_back(id);
        --m_liveCount;
        m_topologyDirty = true; // 删除 instance → 拓扑变化，需重建
    }

    void RayTracingManager::SetInstance(InstanceID id, const TLASInstanceDesc& inst)
    {
        if (id >= m_instances.size() || !m_alive[id]) return;
        m_instances[id] = inst;
        m_transformDirty = true;
    }

    void RayTracingManager::SetInstanceTransform(InstanceID id, const float transform[12])
    {
        if (id >= m_instances.size() || !m_alive[id]) return;
        for (int i = 0; i < 12; ++i) m_instances[id].transform[i] = transform[i];
        m_transformDirty = true;
    }

    std::vector<TLASInstanceDesc> RayTracingManager::CollectLiveInstances() const
    {
        std::vector<TLASInstanceDesc> out;
        out.reserve(m_liveCount);
        for (size_t i = 0; i < m_instances.size(); ++i)
            if (m_alive[i]) out.push_back(m_instances[i]);
        return out;
    }

    void RayTracingManager::RebuildTLAS()
    {
        // 延迟销毁旧 TLAS，保证在飞帧仍可安全使用（走既有 EnqueueDestroy 时序）。
        if (m_tlas.IsValid())
        {
            m_device.Destroy(m_tlas);
            m_tlas = {};
        }

        std::vector<TLASInstanceDesc> live = CollectLiveInstances();
        if (live.empty())
        {
            m_tlasInstanceCount = 0;
            m_topologyDirty = false;
            m_transformDirty = false;
            return;
        }

        AccelerationStructureDesc desc{};
        desc.type = AccelerationStructureType::TopLevel;
        // 带 AllowUpdate，使后续仅 transform 变化时可走 refit（需求 15.3）。
        desc.buildFlags = ASBuildFlags::PreferFastTrace | ASBuildFlags::AllowUpdate;
        desc.instances = std::move(live);
        desc.debugName = "RTASManager.TLAS";

        m_tlas = m_device.CreateAccelerationStructure(desc);
        m_tlasInstanceCount = m_liveCount;
        m_topologyDirty = false;
        m_transformDirty = false;
    }

    AccelerationStructureHandle RayTracingManager::BuildOrRefit(RenderCommandList* cmd)
    {
        // 需重建：首次 / 拓扑变化 / 无命令列表可用于 refit。
        if (!m_tlas.IsValid() || m_topologyDirty || cmd == nullptr)
        {
            RebuildTLAS();
            return m_tlas;
        }

        // 仅 transform 变化 → 命令流内 refit（增量更新，需求 15.3）。
        if (m_transformDirty)
        {
            AccelerationStructureBuildInfo info{};
            info.type = AccelerationStructureType::TopLevel;
            info.buildFlags = ASBuildFlags::PreferFastTrace | ASBuildFlags::AllowUpdate;
            info.instances = CollectLiveInstances();
            info.update = (info.instances.size() == m_tlasInstanceCount);
            info.source = m_tlas;
            cmd->BuildAccelerationStructure(m_tlas, info);
            m_transformDirty = false;
        }
        return m_tlas;
    }

    void RayTracingManager::Clear()
    {
        if (m_tlas.IsValid())
        {
            m_device.Destroy(m_tlas);
            m_tlas = {};
        }
        for (auto& kv : m_blasCache)
            if (kv.second.IsValid()) m_device.Destroy(kv.second);
        m_blasCache.clear();

        m_instances.clear();
        m_alive.clear();
        m_freeList.clear();
        m_liveCount = 0;
        m_tlasInstanceCount = 0;
        m_topologyDirty = false;
        m_transformDirty = false;
    }
}
