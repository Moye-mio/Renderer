#pragma once
// ============================================================================
// RendererCore - GpuMesh
//
// 后端无关的"GPU 网格"：仅持有 BufferHandle / 计数 / 顶点布局描述。
// 不持有任何 GLuint/VkBuffer 等后端原生类型。
// 由 AssetGpuUploader::CreateGpuMesh 创建；由 Pass / DrawCall 消费。
//
// 任务 12 / M5-B。
// ============================================================================
#include <cstdint>
#include <string>

#include "GDescs.h"
#include "GEnums.h"
#include "GHandle.h"

namespace TitusRHI
{
    // 单个 SubMesh：对应 AssetLoader 的一个 MeshAssetData（Assimp 中一个 aiMesh）
    struct GpuSubMesh
    {
        std::string  name;

        BufferHandle vertexBuffer;     // 顶点缓冲（VBO）
        BufferHandle indexBuffer;      // 索引缓冲（IBO，可选）

        uint32_t     vertexCount = 0;
        uint32_t     indexCount  = 0;
        IndexType    indexType   = IndexType::UInt32;

        // 该 SubMesh 在父 GpuMesh 共享 layout 之外的特殊布局；为空表示沿用父级
        VertexLayout overrideLayout;

        // 该 SubMesh 期望的图元拓扑（默认三角形列表，CPU IR 端不携带）
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;

        // CPU 端 AABB（与 MeshAssetData 对齐）
        float aabbMin[3] = { 0.0f, 0.0f, 0.0f };
        float aabbMax[3] = { 0.0f, 0.0f, 0.0f };
    };

    // GpuMesh —— 一个模型的 GPU 表示，由 1..N 个 GpuSubMesh 组成；
    // 与 Material 解耦：调用方在 DrawCall 时把 SubMesh 与 Material 配对。
    struct GpuMesh
    {
        std::string             sourcePath;
        VertexLayout            sharedLayout;     // 所有 SubMesh 默认共用
        std::vector<GpuSubMesh> subMeshes;

        // 一个模型整体的 AABB
        float aabbMin[3] = { 0.0f, 0.0f, 0.0f };
        float aabbMax[3] = { 0.0f, 0.0f, 0.0f };
    };
} // namespace TitusRHI
