#pragma once
// ============================================================================
// AssetLoader - MeshUtils（暂存于 003_Toon_Shading）
//
// 通用网格几何处理，不依赖渲染后端，也不依赖任何具体业务工程。命名空间与
// include 形式已按最终归属写成 AssetLoader 的样子：算法在真实模型上验证过
// 之后，本文件与配套 .cpp 原样移入 AssetLoader/MeshUtils.h / .cpp，代码无需
// 改动，只需调整 vcxproj 与调用方的 include 路径。
// ============================================================================
#include "AssetTypes.h"

#include <cstddef>
#include <vector>

namespace TitusAsset
{
    struct SmoothNormalOptions
    {
        // 位置量化网格。坐标按 1/positionEpsilon 取整后比较，落在同一格才算重合。
        // 注意这是纯栅格判定：跨格线的两点即便距离小于 eps 也不会合并。导出器写出的
        // 重合顶点通常是同一串十进制文本，解析出的 float 逐位相同，够用。
        float positionEpsilon = 1e-4f;

        // 与顶点自身法线夹角超过此值的邻面不参与平均。背靠背的双面片（裙摆、发片）
        // 两侧法线接近相反，不设阈值会互相抵消成零向量。
        float maxSmoothAngleDeg = 85.0f;

        // 按面积加权。面法线未归一化时其长度即两倍三角形面积。
        bool areaWeighted = true;
    };

    struct SmoothNormalStats
    {
        // 分桶后的唯一位置数，与顶点总数的比值反映网格的焊接程度。
        size_t uniquePositions = 0;
        // 平滑后法线相对原法线偏转超过 1 度的顶点数，即真正被"焊平"的那些。
        size_t mergedVertices = 0;
        // 邻面被角度阈值全部滤掉、回退到原法线的顶点数。
        size_t degenerateFallbacks = 0;
    };

    // 跨 mesh 按位置聚合面法线，为每个顶点算一套平滑法线。
    //
    // outNormals[meshIndex][vertexIndex] 与 model.meshes[i].vertices 一一对应。
    // 只产出结果、不改写 model，由调用方决定存进哪个顶点槽。
    //
    // 前提：model 的所有 mesh 处于同一坐标空间。LoadModel 目前平铺遍历
    // scene->mMeshes、不应用节点变换，因此各节点带变换的场景不满足该前提，
    // 跨 mesh 分桶会把实际不重合的点合到一起。
    bool ComputeSmoothNormals(const ModelAssetData& model,
                              std::vector<std::vector<TitusMath::Vec3>>& outNormals,
                              const SmoothNormalOptions& opts = {},
                              SmoothNormalStats* outStats = nullptr);
} // namespace TitusAsset
