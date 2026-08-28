#pragma once
// ============================================================================
// 003_Toon_Shading - OutlineBake
//
// 把描边需要的顶点数据烘进 CPU 侧 ModelAssetData，在 UploadGpuModel 之前调用。
//
//   tangent   ← 平滑法线。原始 normal 保留给 Cel 着色（它需要硬边），描边壳
//               单独用一套跨 mesh 平滑过的法线，否则硬边与 UV 缝处壳体会撕裂。
//   bitangent ← (partIndex, 0, 0)。y / z 空闲。
//
// 占用 tangent / bitangent 的前提是 003 的 ModelLoadOptions 里
// calcTangentSpace = false，这两个槽存的是默认值且没有 shader 读过。代价是
// 将来若要做头发各向异性高光或切线空间法线贴图，得先给切线腾位置。
//
// 描边色、线宽倍率、逐部件开关都不在这里烘：它们放在 OutlineUBO 的
// partParams[] 里按 partIndex 查表，可以在 ImGui 里实时调，比烘死在顶点上有用。
// ============================================================================
#include "AssetLoader/AssetTypes.h"
#include "NilouMaterials.h"
#include "SmoothNormal.h"

#include "Logger.h"

#include <string>
#include <vector>

namespace OutlineBake
{
    // 返回 false 表示平滑法线没算出来，描边会退化成用原始硬边法线外扩。
    inline bool BakeOutlineAttributes(TitusAsset::ModelAssetData& model)
    {
        TitusAsset::SmoothNormalOptions opts{};
        std::vector<std::vector<TitusMath::Vec3>> smoothNormals;
        TitusAsset::SmoothNormalStats stats{};

        if (!TitusAsset::ComputeSmoothNormals(model, smoothNormals, opts, &stats))
        {
            LOG_STREAM_ERROR("OutlineBake") << "ComputeSmoothNormals failed";
            return false;
        }

        size_t vertexTotal = 0;
        for (size_t m = 0; m < model.meshes.size(); ++m)
        {
            auto& mesh = model.meshes[m];
            const std::string& key = !mesh.material.name.empty() ? mesh.material.name
                                                                 : mesh.name;
            const float partIndex =
                static_cast<float>(NilouMaterials::ClassifyPart(key));

            for (size_t v = 0; v < mesh.vertices.size(); ++v)
            {
                mesh.vertices[v].tangent = smoothNormals[m][v];
                mesh.vertices[v].bitangent = TitusMath::Vec3{partIndex, 0.0f, 0.0f};
            }
            vertexTotal += mesh.vertices.size();
        }

        LOG_STREAM_INFO("OutlineBake")
            << "smooth normals: vertices=" << vertexTotal
            << " uniquePositions=" << stats.uniquePositions
            << " merged=" << stats.mergedVertices
            << " fallback=" << stats.degenerateFallbacks
            << " (angle<=" << opts.maxSmoothAngleDeg << "deg)";
        return true;
    }
} // namespace OutlineBake
