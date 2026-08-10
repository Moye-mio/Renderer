#pragma once
// ============================================================================
// RendererInterface - TitusGfxAsset.h
//
// 资源上传链路的对外转发头：业务侧只需 #include <TitusGfxAsset.h>
// 即可使用：
//   - TitusAsset::* （CPU IR；来自 AssetLoader 模块）
//   - TitusRHI::GpuMesh / GpuSubMesh
//   - TitusRHI::MaterialInstance / MaterialTextureSlot / MaterialParameters
//   - TitusRHI::AssetGpuUploader
//
// 让外部模块在不直接 include Renderer/RendererCore/RendererVK
// 的前提下，完成 "AssetLoader::ModelAssetData → GPU 资源" 的整条链路调用。
// ============================================================================

// CPU IR
#include "AssetLoader/AssetLoader.h"
#include "AssetLoader/AssetTypes.h"

// GPU 数据结构（后端无关）
#include "RendererCore/GpuMesh.h"
#include "RendererCore/MaterialInstance.h"
#include "RendererCore/GpuModel.h"

// 上传器（依赖 IGDevice 抽象）
#include "RendererCore/AssetGpuUploader.h"
