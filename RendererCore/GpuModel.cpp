// ============================================================================
// RendererCore - GpuModel.cpp
// GpuMesh + MaterialInstance[] 的统一聚合实现
// ============================================================================
#include "GpuModel.h"

#include "IGDevice.h"

// AssetLoader（CPU IR） —— 仅本 cpp 引用，避免 GpuModel.h 强依赖
#include "AssetLoader/AssetLoader.h"
#include "AssetLoader/ModelLoader.h"

#include <iostream>
#include <unordered_set>
#include "Logger.h"

namespace TitusRHI
{
    bool GpuModel::LoadFromFile(const std::string&                  path,
                                AssetGpuUploader&                   uploader,
                                const AssetUploadOptions&           opts,
                                const TitusAsset::ModelLoadOptions* modelOpts)
    {
        TitusAsset::ModelAssetData asset;
        const bool ok = modelOpts
            ? TitusAsset::LoadModel(path, asset, *modelOpts)
            : TitusAsset::LoadModel(path, asset);
        if (!ok)
        {
            LOG_STREAM_ERROR("GpuModel") << "LoadModel failed: " << path;
            return false;
        }
        return LoadFromAsset(asset, uploader, opts);
    }

    bool GpuModel::LoadFromAsset(const TitusAsset::ModelAssetData& asset,
                                 AssetGpuUploader&                 uploader,
                                 const AssetUploadOptions&         opts)
    {
        // 重新构造，确保覆盖旧数据
        m_mesh      = GpuMesh{};
        m_materials.clear();

        const bool ok = uploader.UploadModel(asset, m_mesh, m_materials, opts);
        if (!ok)
        {
            LOG_STREAM_ERROR("GpuModel") << "UploadModel failed (subMeshes="
                      << asset.meshes.size() << ")";
            m_mesh = GpuMesh{};
            m_materials.clear();
            return false;
        }

        // 不变量校验：subMeshes / materials 一一对应
        if (m_mesh.subMeshes.size() != m_materials.size())
        {
            LOG_STREAM_ERROR("GpuModel") << "mismatched submesh/material count: "
                      << m_mesh.subMeshes.size() << " vs " << m_materials.size();
            return false;
        }
        return true;
    }

    void GpuModel::Release(IGDevice& device)
    {
        // 先释放 SubMesh 的 VB/IB
        for (auto& sm : m_mesh.subMeshes)
        {
            if (sm.vertexBuffer.IsValid()) device.Destroy(sm.vertexBuffer);
            if (sm.indexBuffer.IsValid())  device.Destroy(sm.indexBuffer);
            sm.vertexBuffer = {};
            sm.indexBuffer  = {};
        }
        m_mesh.subMeshes.clear();

        // 再释放 MaterialInstance 引用的 texture/sampler 句柄
        // 注意：纹理可能被多个材质共享（AssetGpuUploader 内部按 ImageAssetData* 去重），
        // 这里通过 set 去重避免重复 Destroy。
        std::unordered_set<uint64_t> destroyedTex;
        std::unordered_set<uint64_t> destroyedSampler;
        for (auto& mat : m_materials)
        {
            for (auto& slot : mat.textures)
            {
                if (slot.texture.IsValid() &&
                    destroyedTex.insert(slot.texture.id).second)
                {
                    device.Destroy(slot.texture);
                }
                if (slot.sampler.IsValid() &&
                    destroyedSampler.insert(slot.sampler.id).second)
                {
                    device.Destroy(slot.sampler);
                }
                slot.texture = {};
                slot.sampler = {};
            }
            // pipeline 由上层 Material/PipelineCache 负责，不在此销毁
        }
        m_materials.clear();
    }
} // namespace TitusRHI
