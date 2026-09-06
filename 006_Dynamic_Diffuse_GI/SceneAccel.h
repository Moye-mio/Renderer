#pragma once
// ============================================================================
// 006_Dynamic_Diffuse_GI - SceneAccel
// 从 CPU 侧 ModelAssetData 打一份紧凑几何：
//   - float3 位置缓冲 + uint 索引：建 Sponza 级 BLAS / TLAS
//   - Vertex SSBO（位置 / 法线 / 烘好的 albedo）：rayQuery 命中后插值着色
// 每个 SubMesh 是 BLAS 里的一条 geometry，geometryIndex 就是 mesh 下标。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

#include <cstdint>
#include <vector>

namespace TitusAsset
{
    struct ModelAssetData;
}

class SceneAccel
{
public:
    bool Build(TitusRHI::IGDevice& device, const TitusAsset::ModelAssetData& model);
    void Destroy(TitusRHI::IGDevice& device);

    bool IsValid() const { return m_tlas.IsValid(); }
    TitusRHI::AccelerationStructureHandle GetTLAS() const { return m_tlas; }

    TitusRHI::BufferHandle GetVertexBuffer() const { return m_vertexSSBO; }
    TitusRHI::BufferHandle GetIndexBuffer() const { return m_indexBuffer; }
    TitusRHI::BufferHandle GetMeshRangeBuffer() const { return m_meshRangeBuffer; }

    uint32_t GetMeshCount() const { return m_meshCount; }
    uint32_t GetVertexCount() const { return m_vertexCount; }
    uint32_t GetIndexCount() const { return m_indexCount; }

    TitusMath::Vec3 GetAabbMin() const { return m_aabbMin; }
    TitusMath::Vec3 GetAabbMax() const { return m_aabbMax; }

private:
    TitusRHI::BufferHandle m_positionBuffer;
    TitusRHI::BufferHandle m_indexBuffer;
    TitusRHI::BufferHandle m_vertexSSBO;
    TitusRHI::BufferHandle m_meshRangeBuffer;
    TitusRHI::AccelerationStructureHandle m_blas;
    TitusRHI::AccelerationStructureHandle m_tlas;

    uint32_t m_meshCount = 0;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
    TitusMath::Vec3 m_aabbMin{0.0f};
    TitusMath::Vec3 m_aabbMax{0.0f};
};
