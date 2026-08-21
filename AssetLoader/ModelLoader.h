#pragma once
// ============================================================================
// AssetLoader - ModelLoader.h
// 加载完整的模型（OBJ/FBX 等 Assimp 支持的格式）为 CPU 端 ModelAssetData。
// 可选地加载所有引用的纹理像素（loadTextures=true）。
// ============================================================================
#include "AssetTypes.h"

namespace TitusAsset
{
    struct ModelLoadOptions
    {
        bool triangulate          = true;   // aiProcess_Triangulate
        bool flipUVs              = true;   // aiProcess_FlipUVs
        bool generateNormals      = true;   // aiProcess_GenSmoothNormals
        bool calcTangentSpace     = true;   // aiProcess_CalcTangentSpace
        bool loadTextures         = true;   // 同时把材质贴图像素加载到 sharedImages
        bool flipVerticallyOnLoad = true;   // 透传给 ImageLoader
        bool preTransformVertices = false;  // aiProcess_PreTransformVertices：烘焙节点变换，仅静态展示
        bool fbxReadAnimations    = true;   // AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS
        bool fbxReadCameras       = true;
        bool fbxReadLights        = true;
        bool fbxPreservePivots    = true;   // 部分蒙皮 FBX 关掉可避免 Assimp 崩溃
    };

    // 从磁盘加载模型；失败返回 false
    bool LoadModel(const std::string& path,
                   ModelAssetData& outModel,
                   const ModelLoadOptions& opts = {});
} // namespace TitusAsset
