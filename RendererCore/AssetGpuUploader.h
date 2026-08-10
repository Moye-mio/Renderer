#pragma once
// ============================================================================
// RendererCore - AssetGpuUploader
// 把 AssetLoader 输出的纯 CPU IR（TitusAsset::*）通过 IGDevice 上传成
// 后端无关的 GPU 资源（GpuMesh / Material / TextureHandle）。
// 单向依赖：
//   AssetLoader (CPU) ◄── AssetGpuUploader ──► IGDevice (GPU)
// 不引入：
//   - 任何 OpenGL / Vulkan 头文件
//   - Renderer/RendererVK 项目（仅依赖 IGDevice 抽象）
// ============================================================================
#include <memory>
#include <string>
#include <unordered_map>

#include "GpuMesh.h"
#include "MaterialInstance.h"
#include "GHandle.h"

// 前向声明 AssetLoader 的类型，避免在头文件中拉入 AssetLoader 头
namespace TitusAsset
{
    struct ImageAssetData;
    struct MeshAssetData;
    struct ModelAssetData;
    struct MaterialAssetData;
}

namespace TitusRHI
{
    class IGDevice;

    // 单个上传任务的可调选项
    struct AssetUploadOptions
    {
        // 是否生成 mipmap（GL 端：glGenerateMipmap；VK 端：blit chain）
        bool generateMipmaps = false;

        // 默认图形管线：当上传 Material 时若 AssetMaterial 不指定 shader，
        // 则把这个 pipeline 写入 Material.pipeline。可为非法句柄。
        PipelineHandle defaultPipeline;

        // 默认采样器：当上传纹理后若调用方未提供 sampler，使用此句柄。
        // 为非法时由 AssetGpuUploader::EnsureDefaultSampler 创建一个 Repeat+Linear。
        SamplerHandle  defaultSampler;
    };

    // ------------------------------------------------------------------------
    // AssetGpuUploader
        // 持有 IGDevice 引用（不拥有），并在内部缓存"已上传的图像 → TextureHandle"
    // 的映射，避免同一张 sharedImage 被重复上传。
    // 析构时释放缓存中的纹理；vertex/index buffer 与 sampler 的所有权由
    // GpuMesh / Material 的持有者管理。
    // ------------------------------------------------------------------------
    class AssetGpuUploader
    {
    public:
        AssetGpuUploader() = default;
        explicit AssetGpuUploader(IGDevice* device);
        ~AssetGpuUploader();

        // 不可拷贝、可移动
        AssetGpuUploader(const AssetGpuUploader&) = delete;
        AssetGpuUploader& operator=(const AssetGpuUploader&) = delete;
        AssetGpuUploader(AssetGpuUploader&&) noexcept = default;
        AssetGpuUploader& operator=(AssetGpuUploader&&) noexcept = default;

        // 设置/替换设备引用；若之前缓存了纹理，会自动调用旧设备 Destroy
        void SetDevice(IGDevice* device);
        IGDevice* GetDevice() const noexcept { return m_device; }

        // 一次性上传整个 ModelAsset：返回 GpuMesh + 与之同长度的 MaterialInstance 数组
        // outMaterials.size() == outMesh.subMeshes.size()，按顺序对应
        bool UploadModel(const TitusAsset::ModelAssetData& asset,
                         GpuMesh&                          outMesh,
                         std::vector<MaterialInstance>&    outMaterials,
                         const AssetUploadOptions&         opts = {});

        // 单个 SubMesh 上传：仅几何（顶点 + 索引），不涉及材质 / 纹理
        bool UploadSubMesh(const TitusAsset::MeshAssetData& mesh,
                           GpuSubMesh&                      outSubMesh,
                           const AssetUploadOptions&        opts = {});

        // 上传单张纹理；多次调用同一 ImageAssetData* 会命中缓存返回相同句柄
        TextureHandle UploadTexture(const std::shared_ptr<TitusAsset::ImageAssetData>& image,
                                    const AssetUploadOptions& opts = {});

        // 把 CPU 端 MaterialAsset 转成 GPU MaterialInstance（按需上传所引用的纹理）
        bool UploadMaterial(const TitusAsset::MaterialAssetData& assetMat,
                            const std::unordered_map<std::string,
                                  std::shared_ptr<TitusAsset::ImageAssetData>>& imageBySrcPath,
                            MaterialInstance&          outMaterial,
                            const AssetUploadOptions&  opts = {});

        // 显式清空缓存（销毁所有缓存的 TextureHandle 与 default sampler）
        void Reset();

    private:
        TextureHandle UploadTextureInternal(const TitusAsset::ImageAssetData& image,
                                            bool                              generateMipmaps);
        SamplerHandle EnsureDefaultSampler();

        IGDevice*   m_device = nullptr;
        SamplerHandle m_defaultSampler;
        // 以 ImageAssetData* 为 key（同一份 sharedImage 在 ModelAssetData 中只出现一次）
        std::unordered_map<const TitusAsset::ImageAssetData*, TextureHandle> m_imageCache;
    };
} // namespace TitusRHI
