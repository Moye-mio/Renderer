#pragma once
// ============================================================================
// AssetLoader - MeshLoader.h
// 把 Assimp 的 aiMesh / aiMaterial 解析为纯 CPU 的 MeshAssetData。
// 不进行任何 GPU VAO/VBO 创建。
// ============================================================================
#include "AssetTypes.h"

struct aiMesh;
struct aiMaterial;
struct aiScene;

namespace TitusAsset
{
    // 从一个 aiMesh + 对应 aiMaterial 解析出 MeshAssetData
    // - directory: 原始模型目录，用于解析 material 内嵌纹理的相对路径
    bool BuildMeshFromAi(const aiMesh* mesh,
                         const aiScene* scene,
                         const std::string& directory,
                         MeshAssetData& outMesh);

    // 从一个 aiMaterial 抽取材质属性（颜色、贴图引用等），不加载纹理像素
    bool BuildMaterialFromAi(const aiMaterial* mat,
                             const std::string& directory,
                             MaterialAssetData& outMaterial);
} // namespace TitusAsset
