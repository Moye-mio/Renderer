#pragma once
// ============================================================================
// 003_Toon_Shading - NilouMaterials
//
// 不解析 Unity YAML。按网格 / 材质名过滤，并给出 Diffuse / ilm / Ramp 路径。
// ilm / Ramp 不进 TextureSlot：由 ToonPass 自管 TextureHandle。
// ============================================================================
#include "AssetLoader/AssetTypes.h"

#include <cstdint>
#include <string>

namespace NilouMaterials
{
    enum class Part : uint8_t
    {
        Body = 0,
        Dress,
        Hair,
        Face,
    };

    struct NprTextureFiles
    {
        const char* ilm  = nullptr; // 线性 Lightmap；Face 为 nullptr
        const char* ramp = nullptr; // sRGB Shadow Ramp，必须 Clamp
    };

    bool KeepMesh(const TitusAsset::MeshAssetData& mesh);

    // 网格名或材质名均可（OBJ 里两者都带 Mat_Hair / Mat_Body / …）。
    Part ClassifyPart(const std::string& name);

    NprTextureFiles FilesForPart(Part part);

    // 按材质名绑 Diffuse，填充 sharedImages。
    // 成功条件：至少留下一个网格，且每个留下的网格都有有效 Diffuse。
    bool FilterAndBindDiffuse(TitusAsset::ModelAssetData& model,
                              const std::string& textureDir);
}
