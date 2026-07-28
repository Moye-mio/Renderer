// ============================================================================
// RendererCore - AssetGpuUploader.cpp
//
// 把 TitusAsset::* IR 上传成 IGDevice 的 GPU 资源。所有路径都 #include
// AssetLoader 头（cpp 内部依赖；头文件层面仍是前向声明）。
// ============================================================================
#include "AssetGpuUploader.h"

#include <cstring>
#include <cstdio>
#include "Logger.h"

#include "IGDevice.h"
#include "GDescs.h"
#include "GEnums.h"

#include "AssetLoader/AssetTypes.h"

namespace TitusRHI
{
    namespace
    {
        // ---- AssetLoader.MaterialTextureRef.slot → MaterialTextureSlot 直接 cast ----
        constexpr MaterialTextureSlot ToGfxSlot(TitusAsset::TextureSlot s)
        {
            // 两侧顺序刻意保持一致；这里是编译期常量映射
            return static_cast<MaterialTextureSlot>(static_cast<uint8_t>(s));
        }

        // 简单的 channels + pixelType → Format 映射
        Format PickFormat(const TitusAsset::ImageAssetData& img)
        {
            if (img.isCompressed)
            {
                // 简化：gli format token 不再二次映射（GL/VK 后端自行扩展）
                return Format::R8G8B8A8_UNORM;
            }
            const bool srgb = (img.colorSpace == TitusAsset::ImageColorSpace::SRGB);

            switch (img.pixelType)
            {
            case TitusAsset::ImagePixelType::Float32:
                switch (img.channels)
                {
                case 1: return Format::R32_SFLOAT;
                case 2: return Format::R32G32_SFLOAT;
                case 3: return Format::R32G32B32_SFLOAT;
                case 4: return Format::R32G32B32A32_SFLOAT;
                default: break;
                }
                break;
            case TitusAsset::ImagePixelType::Float16:
                switch (img.channels)
                {
                case 1: return Format::R16_SFLOAT;
                case 2: return Format::R16G16_SFLOAT;
                case 4: return Format::R16G16B16A16_SFLOAT;
                default: break;
                }
                break;
            case TitusAsset::ImagePixelType::UNorm8:
            default:
                switch (img.channels)
                {
                case 1: return Format::R8_UNORM;
                case 2: return Format::R8G8_UNORM;
                // RendererCore::GEnums 当前未提供 R8G8B8_SRGB 三通道 SRGB 格式
                case 3: return Format::R8G8B8_UNORM;
                case 4: return srgb ? Format::R8G8B8A8_SRGB : Format::R8G8B8A8_UNORM;
                default: break;
                }
                break;
            }
            return Format::R8G8B8A8_UNORM;
        }

        // 与 TitusAsset::MeshVertex 字段顺序对齐的 VertexLayout
        VertexLayout BuildMeshVertexLayout()
        {
            // 该顶点结构体定义见 AssetTypes.h::MeshVertex（pos/normal/uv/tangent/bitangent）
            const uint32_t stride =
                  3 * sizeof(float)  // position
                + 3 * sizeof(float)  // normal
                + 2 * sizeof(float)  // uv
                + 3 * sizeof(float)  // tangent
                + 3 * sizeof(float); // bitangent

            VertexLayout layout;
            VertexBinding b{};
            b.binding   = 0;
            b.stride    = stride;
            b.inputRate = VertexInputRate::Vertex;
            layout.bindings.push_back(b);

            uint32_t offset = 0;
            auto add = [&](uint32_t loc, Format fmt, uint32_t bytes)
            {
                VertexAttribute a{};
                a.location = loc;
                a.binding  = 0;
                a.format   = fmt;
                a.offset   = offset;
                offset += bytes;
                layout.attributes.push_back(a);
            };
            add(0, Format::R32G32B32_SFLOAT, 12); // position
            add(1, Format::R32G32B32_SFLOAT, 12); // normal
            add(2, Format::R32G32_SFLOAT,    8 ); // uv
            add(3, Format::R32G32B32_SFLOAT, 12); // tangent
            add(4, Format::R32G32B32_SFLOAT, 12); // bitangent
            return layout;
        }
    } // anonymous

    // ========================================================================
    // 生命周期
    // ========================================================================
    AssetGpuUploader::AssetGpuUploader(IGDevice* device)
        : m_device(device)
    {
    }

    AssetGpuUploader::~AssetGpuUploader()
    {
        Reset();
    }

    void AssetGpuUploader::SetDevice(IGDevice* device)
    {
        if (device == m_device)
            return;
        Reset();
        m_device = device;
    }

    void AssetGpuUploader::Reset()
    {
        if (!m_device)
        {
            m_imageCache.clear();
            m_defaultSampler = SamplerHandle{};
            return;
        }
        for (auto& kv : m_imageCache)
        {
            if (kv.second.IsValid())
                m_device->Destroy(kv.second);
        }
        m_imageCache.clear();
        if (m_defaultSampler.IsValid())
        {
            m_device->Destroy(m_defaultSampler);
            m_defaultSampler = SamplerHandle{};
        }
    }

    // ========================================================================
    // 默认采样器
    // ========================================================================
    SamplerHandle AssetGpuUploader::EnsureDefaultSampler()
    {
        if (m_defaultSampler.IsValid() || !m_device)
            return m_defaultSampler;

        SamplerDesc sd;
        sd.minFilter   = FilterMode::Linear;
        sd.magFilter   = FilterMode::Linear;
        sd.mipmapMode  = MipmapMode::Linear;
        sd.addressU    = AddressMode::Repeat;
        sd.addressV    = AddressMode::Repeat;
        sd.addressW    = AddressMode::Repeat;
        sd.anisotropyEnable = true;
        sd.maxAnisotropy    = 8.0f;
        sd.debugName        = "AssetGpuUploader::DefaultSampler";
        m_defaultSampler = m_device->CreateSampler(sd);
        return m_defaultSampler;
    }

    // ========================================================================
    // 单张纹理上传
    // ========================================================================
    TextureHandle
    AssetGpuUploader::UploadTextureInternal(const TitusAsset::ImageAssetData& image,
                                            bool generateMipmaps)
    {
        if (!m_device || image.mips.empty())
            return TextureHandle{};

        // 任务 10：RGB（3 通道）→ RGBA（4 通道）自动扩展。
        // 原因：R8G8B8_UNORM / R8G8B8_SRGB 在桌面 GPU（包括 NVIDIA）大多不被原生支持，
        // vkCreateImage 会返回 VK_ERROR_FORMAT_NOT_SUPPORTED；R32G32B32_SFLOAT 同理。
        // 这里在 host 侧把每像素 3 通道扩成 4 通道（alpha=255 / 1.0），让 GL/VK 两端
        // 都走 4 通道路径，避免后端判断分支。
        const TitusAsset::ImageAssetData* effectiveImage = &image;
        TitusAsset::ImageAssetData expanded;            // 仅在需要扩展时使用
        const bool needExpandRGBToRGBA =
            (image.channels == 3) &&
            (image.pixelType == TitusAsset::ImagePixelType::UNorm8 ||
             image.pixelType == TitusAsset::ImagePixelType::Float32);
        if (needExpandRGBToRGBA)
        {
            expanded = image;          // 浅复制 width / height / channels / mips...
            expanded.channels = 4;
            expanded.mips.clear();
            expanded.mips.reserve(image.mips.size());

            const bool isFloat = (image.pixelType == TitusAsset::ImagePixelType::Float32);
            const size_t srcChannelBytes = isFloat ? 4 : 1;
            for (const auto& srcMip : image.mips)
            {
                TitusAsset::ImageMipLevel dstMip;
                dstMip.width  = srcMip.width;
                dstMip.height = srcMip.height;
                const size_t pixelCount = static_cast<size_t>(srcMip.width) *
                                          static_cast<size_t>(srcMip.height);
                dstMip.pixels.bytes.resize(pixelCount * 4 * srcChannelBytes);

                const uint8_t* src = srcMip.pixels.data();
                uint8_t*       dst = dstMip.pixels.data();
                if (isFloat)
                {
                    const float* srcF = reinterpret_cast<const float*>(src);
                    float*       dstF = reinterpret_cast<float*>(dst);
                    for (size_t p = 0; p < pixelCount; ++p)
                    {
                        dstF[p * 4 + 0] = srcF[p * 3 + 0];
                        dstF[p * 4 + 1] = srcF[p * 3 + 1];
                        dstF[p * 4 + 2] = srcF[p * 3 + 2];
                        dstF[p * 4 + 3] = 1.0f;
                    }
                }
                else
                {
                    for (size_t p = 0; p < pixelCount; ++p)
                    {
                        dst[p * 4 + 0] = src[p * 3 + 0];
                        dst[p * 4 + 1] = src[p * 3 + 1];
                        dst[p * 4 + 2] = src[p * 3 + 2];
                        dst[p * 4 + 3] = 255;
                    }
                }
                expanded.mips.push_back(std::move(dstMip));
            }
            effectiveImage = &expanded;
        }

        TextureDesc desc;
        desc.type        = effectiveImage->isCubeMap ? TextureType::TexCube : TextureType::Tex2D;
        desc.format      = PickFormat(*effectiveImage);
        desc.width       = effectiveImage->width;
        desc.height      = effectiveImage->height;
        desc.depth       = 1;
        desc.mipLevels   = generateMipmaps ? 0 : 1; // 0 表示由后端按 max 计算
        desc.arrayLayers = effectiveImage->arrayLayers;
        desc.samples     = 1;
        desc.usage       = TextureUsage::Sampled | TextureUsage::TransferDst;
        desc.debugName   = effectiveImage->sourcePath.c_str();

        TextureHandle tex = m_device->CreateTexture(desc);
        if (!tex.IsValid())
        {
            LOG_ERROR("AssetGpuUploader",
                     "CreateTexture failed: %s",
                     effectiveImage->sourcePath.c_str());
            return tex;
        }

        // CubeMap：6 个面分别上传到 arrayLayer=0..5；2D：单面上传 mip0
        if (effectiveImage->isCubeMap)
        {
            const uint32_t faces = (effectiveImage->arrayLayers <= effectiveImage->mips.size())
                                  ? effectiveImage->arrayLayers
                                  : static_cast<uint32_t>(effectiveImage->mips.size());
            for (uint32_t face = 0; face < faces; ++face)
            {
                const auto& mip = effectiveImage->mips[face];
                TextureUploadDesc up{};
                up.data       = mip.pixels.data();
                up.bytes      = mip.pixels.size();
                up.mipLevel   = 0;
                up.arrayLayer = face;
                up.width      = mip.width;
                up.height     = mip.height;
                up.depth      = 1;
                m_device->UpdateTexture(tex, up);
            }
        }
        else
        {
            const auto& mip = effectiveImage->mips.front();
            TextureUploadDesc up{};
            up.data       = mip.pixels.data();
            up.bytes      = mip.pixels.size();
            up.mipLevel   = 0;
            up.arrayLayer = 0;
            up.width      = mip.width;
            up.height     = mip.height;
            up.depth      = 1;
            m_device->UpdateTexture(tex, up);
        }
        return tex;
    }

    TextureHandle
    AssetGpuUploader::UploadTexture(
        const std::shared_ptr<TitusAsset::ImageAssetData>& image,
        const AssetUploadOptions& opts)
    {
        if (!m_device || !image)
            return TextureHandle{};
        // 命中缓存
        if (auto it = m_imageCache.find(image.get()); it != m_imageCache.end())
            return it->second;

        const TextureHandle tex = UploadTextureInternal(*image, opts.generateMipmaps);
        if (tex.IsValid())
            m_imageCache.emplace(image.get(), tex);
        return tex;
    }

    // ========================================================================
    // SubMesh 几何上传
    // ========================================================================
    bool AssetGpuUploader::UploadSubMesh(const TitusAsset::MeshAssetData& mesh,
                                         GpuSubMesh&                      outSubMesh,
                                         const AssetUploadOptions&        /*opts*/)
    {
        if (!m_device) return false;

        outSubMesh = GpuSubMesh{};
        outSubMesh.name        = mesh.name;
        outSubMesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
        outSubMesh.indexCount  = static_cast<uint32_t>(mesh.indices.size());
        outSubMesh.indexType   = IndexType::UInt32;
        outSubMesh.topology    = PrimitiveTopology::TriangleList;
        outSubMesh.aabbMin[0]  = mesh.aabbMin.x;
        outSubMesh.aabbMin[1]  = mesh.aabbMin.y;
        outSubMesh.aabbMin[2]  = mesh.aabbMin.z;
        outSubMesh.aabbMax[0]  = mesh.aabbMax.x;
        outSubMesh.aabbMax[1]  = mesh.aabbMax.y;
        outSubMesh.aabbMax[2]  = mesh.aabbMax.z;

        // VBO
        if (!mesh.vertices.empty())
        {
            BufferDesc vbDesc;
            vbDesc.size  = sizeof(TitusAsset::MeshVertex) * mesh.vertices.size();
            vbDesc.usage = BufferUsage::VertexBuffer | BufferUsage::TransferDst;
            vbDesc.memory = MemoryUsage::GpuOnly;
            vbDesc.initialData = mesh.vertices.data();
            vbDesc.debugName   = mesh.name.empty() ? "Mesh.VBO" : mesh.name.c_str();
            outSubMesh.vertexBuffer = m_device->CreateBuffer(vbDesc);
        }

        // IBO
        if (!mesh.indices.empty())
        {
            BufferDesc ibDesc;
            ibDesc.size  = sizeof(uint32_t) * mesh.indices.size();
            ibDesc.usage = BufferUsage::IndexBuffer | BufferUsage::TransferDst;
            ibDesc.memory = MemoryUsage::GpuOnly;
            ibDesc.initialData = mesh.indices.data();
            ibDesc.debugName   = "Mesh.IBO";
            outSubMesh.indexBuffer = m_device->CreateBuffer(ibDesc);
        }
        return outSubMesh.vertexBuffer.IsValid();
    }

    // ========================================================================
    // 材质上传
    // ========================================================================
    bool AssetGpuUploader::UploadMaterial(
        const TitusAsset::MaterialAssetData& assetMat,
        const std::unordered_map<std::string,
              std::shared_ptr<TitusAsset::ImageAssetData>>& imageBySrcPath,
        MaterialInstance&                    outMaterial,
        const AssetUploadOptions&            opts)
    {
        if (!m_device) return false;

        outMaterial = MaterialInstance{};
        outMaterial.name           = assetMat.name;
        outMaterial.pipeline       = opts.defaultPipeline;
        outMaterial.params.ambientColor   = assetMat.ambientColor;
        outMaterial.params.diffuseColor   = assetMat.diffuseColor;
        outMaterial.params.specularColor  = assetMat.specularColor;
        outMaterial.params.shininess      = assetMat.shininess;
        outMaterial.params.refractiveIndex= assetMat.refractiveIndex;

        const SamplerHandle defSampler = opts.defaultSampler.IsValid()
                                       ? opts.defaultSampler
                                       : EnsureDefaultSampler();

        for (const auto& tref : assetMat.textures)
        {
            const auto slot = ToGfxSlot(tref.slot);
            if (static_cast<size_t>(slot) >=
                static_cast<size_t>(MaterialTextureSlot::SlotCount))
                continue;

            // 通过 path 在共享图像表中找到对应 ImageAssetData
            auto it = imageBySrcPath.find(tref.path);
            if (it == imageBySrcPath.end() || !it->second)
                continue;

            TextureHandle tex = UploadTexture(it->second, opts);
            if (!tex.IsValid()) continue;

            auto& bind = outMaterial.TextureAt(slot);
            bind.texture = tex;
            bind.sampler = defSampler;
            bind.isSRGB  = tref.isSRGB;
        }
        return true;
    }

    // ========================================================================
    // 整模型上传
    // ========================================================================
    bool AssetGpuUploader::UploadModel(const TitusAsset::ModelAssetData& asset,
                                       GpuMesh&                          outMesh,
                                       std::vector<MaterialInstance>&    outMaterials,
                                       const AssetUploadOptions&         opts)
    {
        if (!m_device) return false;

        outMesh = GpuMesh{};
        outMesh.sourcePath   = asset.sourcePath;
        outMesh.sharedLayout = BuildMeshVertexLayout();
        outMesh.subMeshes.reserve(asset.meshes.size());

        outMaterials.clear();
        outMaterials.reserve(asset.meshes.size());

        // 1) 先把 sharedImages 按 sourcePath 建立一次性查找表（O(N) → O(1)）
        std::unordered_map<std::string,
                           std::shared_ptr<TitusAsset::ImageAssetData>> imageMap;
        imageMap.reserve(asset.sharedImages.size());
        for (const auto& img : asset.sharedImages)
        {
            if (img) imageMap.emplace(img->sourcePath, img);
        }

        // 2) 计算整模型 AABB（在 SubMesh 一一上传时累加）
        bool firstAabb = true;
        float mn[3] = { 0, 0, 0 };
        float mx[3] = { 0, 0, 0 };

        for (const auto& m : asset.meshes)
        {
            GpuSubMesh sub;
            if (!UploadSubMesh(m, sub, opts))
                continue;

            if (firstAabb)
            {
                mn[0] = sub.aabbMin[0]; mn[1] = sub.aabbMin[1]; mn[2] = sub.aabbMin[2];
                mx[0] = sub.aabbMax[0]; mx[1] = sub.aabbMax[1]; mx[2] = sub.aabbMax[2];
                firstAabb = false;
            }
            else
            {
                for (int i = 0; i < 3; ++i)
                {
                    if (sub.aabbMin[i] < mn[i]) mn[i] = sub.aabbMin[i];
                    if (sub.aabbMax[i] > mx[i]) mx[i] = sub.aabbMax[i];
                }
            }

            outMesh.subMeshes.push_back(std::move(sub));

            MaterialInstance mat;
            UploadMaterial(m.material, imageMap, mat, opts);
            outMaterials.push_back(std::move(mat));
        }

        for (int i = 0; i < 3; ++i)
        {
            outMesh.aabbMin[i] = mn[i];
            outMesh.aabbMax[i] = mx[i];
        }
        return !outMesh.subMeshes.empty();
    }
} // namespace TitusRHI
