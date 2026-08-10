#pragma once
// ============================================================================
// RendererCore - GpuModel
// 跨后端 Model 包装：GpuMesh + 与 SubMesh 一一对应的 MaterialInstance 数组 + AABB。
// 充当 "AssetLoader CPU IR" 与 "业务侧渲染逻辑" 的统一桥接对象。
// 与遗留 Renderer/Model 的关系：
//   - 遗留 Model 直接持有 GLuint VAO/Texture，与 GL 链路耦合（不强制下线）
//   - GpuModel 仅持有跨后端 Handle，可在 GL/VK 任意后端复用
//   - 业务代码二选一：新 Pass 走 GpuModel；旧 Pass 维持 Renderer/Model
// ============================================================================
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "GpuMesh.h"
#include "MaterialInstance.h"
#include "AssetGpuUploader.h"

namespace TitusAsset
{
    struct ModelAssetData;
    struct ModelLoadOptions;
}

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // GpuModel —— "导入态"模型对象。
    // 字段约束：m_materials.size() == m_mesh.subMeshes.size()
    //          下标 i 对应同一 SubMesh 的材质参数。
    // 默认构造为空模型；通过 LoadFromFile / SetData 填充。
    // ------------------------------------------------------------------------
    class GpuModel
    {
    public:
        GpuModel() = default;
        ~GpuModel() = default;

        GpuModel(const GpuModel&) = delete;
        GpuModel& operator=(const GpuModel&) = delete;
        GpuModel(GpuModel&&) noexcept = default;
        GpuModel& operator=(GpuModel&&) noexcept = default;

        // 一站式入口：磁盘文件 → CPU IR → GPU 上传。
        // - 内部调用 TitusAsset::LoadModel(path, asset, modelOpts)
        // - 再调用 uploader.UploadModel(asset, mesh, materials, opts)
        // 任一环节失败都返回 false。
        bool LoadFromFile(const std::string&            path,
                          AssetGpuUploader&             uploader,
                          const AssetUploadOptions&     opts      = {},
                          const TitusAsset::ModelLoadOptions* modelOpts = nullptr);

        // 已经手上有 CPU IR（例如外部缓存）时直接走这个：
        bool LoadFromAsset(const TitusAsset::ModelAssetData& asset,
                           AssetGpuUploader&                 uploader,
                           const AssetUploadOptions&         opts = {});

        // 直接 setter（高级用法 / 单测构造）
        void SetData(GpuMesh&& mesh, std::vector<MaterialInstance>&& mats)
        {
            m_mesh      = std::move(mesh);
            m_materials = std::move(mats);
        }

        // 释放所有 GPU 资源（buffer / texture / sampler 句柄）。
        // 调用后 GpuModel 进入空状态，可再次 LoadFromFile。
        void Release(IGDevice& device);

        // -------- 访问器 --------
        const GpuMesh&                         GetMesh()      const { return m_mesh; }
        GpuMesh&                               MutableMesh()        { return m_mesh; }
        const std::vector<MaterialInstance>&   GetMaterials() const { return m_materials; }
        std::vector<MaterialInstance>&         MutableMaterials()   { return m_materials; }

        size_t                                 GetSubMeshCount() const { return m_mesh.subMeshes.size(); }
        bool                                   IsValid()         const { return GetSubMeshCount() > 0; }

        const MaterialInstance&  GetMaterial(size_t i) const { return m_materials[i]; }
        MaterialInstance&        GetMaterial(size_t i)       { return m_materials[i]; }

        // 模型在本地坐标系下的 AABB（来自 m_mesh.aabbMin/aabbMax，由 AssetGpuUploader 合并写入）
        void GetAABB(float (&outMin)[3], float (&outMax)[3]) const
        {
            outMin[0] = m_mesh.aabbMin[0]; outMin[1] = m_mesh.aabbMin[1]; outMin[2] = m_mesh.aabbMin[2];
            outMax[0] = m_mesh.aabbMax[0]; outMax[1] = m_mesh.aabbMax[1]; outMax[2] = m_mesh.aabbMax[2];
        }

    private:
        GpuMesh                       m_mesh{};
        std::vector<MaterialInstance> m_materials;
    };
} // namespace TitusRHI
