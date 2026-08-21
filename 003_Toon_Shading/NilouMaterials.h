#pragma once
// ============================================================================
// 003_Toon_Shading - NilouMaterials
//
// 不解析 Unity YAML。按网格名过滤、按材质名把 Diffuse PNG 填进 CPU IR。
// M2 起再在此扩展 ilm / Ramp / Face SDF 路径。
// ============================================================================
#include "AssetLoader/AssetTypes.h"

#include <string>

namespace NilouMaterials
{
    bool KeepMesh(const TitusAsset::MeshAssetData& mesh);

    // 按材质名绑 Diffuse，填充 sharedImages。占位网格已由导出脚本剔除，
    // 眼睛与眉毛片则并入了 Mat_Face 组。
    // 成功条件：至少留下一个网格，且每个留下的网格都有有效 Diffuse。
    bool FilterAndBindDiffuse(TitusAsset::ModelAssetData& model,
                              const std::string& textureDir);
}
