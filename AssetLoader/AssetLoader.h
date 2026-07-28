#pragma once
// ============================================================================
// AssetLoader 模块统一入口
//
// 业务侧使用方式：
//   #include "AssetLoader/AssetLoader.h"
//   TitusAsset::ModelAssetData model;
//   TitusAsset::LoadModel("Model/Sponza/sponza.obj", model);
//
//   // 通过 model.meshes / model.sharedImages 把数据交给上层 GPU 资源管理
//
// 模块严格遵守"零 GPU 依赖"：不 include 任何 OpenGL / Vulkan / RendererCore
// 头文件；任何渲染后端都可消费这里的 ModelAssetData / ImageAssetData。
// ============================================================================
#include "AssetTypes.h"
#include "FileSystem.h"
#include "ImageLoader.h"
#include "MeshLoader.h"
#include "ModelLoader.h"

namespace TitusAsset
{
    // 当前 AssetLoader 库的版本字符串（语义化版本）
    const char* GetAssetLoaderVersion();
} // namespace TitusAsset
