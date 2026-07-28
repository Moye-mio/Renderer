// ============================================================================
// AssetLoader - ImageLoader.cpp
//
// 使用 stb_image 处理 PNG/JPG/HDR；使用 gli 处理 DDS/KTX。
// 完全 CPU 侧操作；输出 ImageAssetData。
// ============================================================================
#include "ImageLoader.h"

#include <cstring>
#include <cstdio>
#include "Logger.h"

#include "FileSystem.h"

// stb_image 是 header-only。AssetLoader 在此 TU 自带 STB_IMAGE_IMPLEMENTATION，
// 作为 stbi_* 符号的唯一定义处（原先由 RendererGL 旧文件提供，已随旧路径清退）。
// 全仓仅此一处定义，不会与其他 TU 重复。
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <gli.hpp>

namespace TitusAsset
{
    namespace
    {
        std::shared_ptr<ImageAssetData> LoadStbi(const std::string& path,
                                                 const ImageLoadOptions& opts,
                                                 bool isHDR)
        {
            stbi_set_flip_vertically_on_load(opts.flipVerticallyOnLoad ? 1 : 0);

            int w = 0, h = 0, chans = 0;
            void* pixels = nullptr;
            if (isHDR)
                pixels = stbi_loadf(path.c_str(), &w, &h, &chans, opts.desiredChannels);
            else
                pixels = stbi_load (path.c_str(), &w, &h, &chans, opts.desiredChannels);

            if (!pixels)
            {
                LOG_ERROR("AssetLoader::ImageLoader",
                          "stbi load failed: %s, reason=%s",
                          path.c_str(), stbi_failure_reason());
                return nullptr;
            }

            const int outChannels = (opts.desiredChannels > 0) ? opts.desiredChannels : chans;

            auto img = std::make_shared<ImageAssetData>();
            img->sourcePath = path;
            img->pixelType  = isHDR ? ImagePixelType::Float32 : ImagePixelType::UNorm8;
            img->colorSpace = (opts.isSRGBHint && !isHDR) ? ImageColorSpace::SRGB : ImageColorSpace::Linear;
            img->width      = static_cast<uint32_t>(w);
            img->height     = static_cast<uint32_t>(h);
            img->channels   = static_cast<uint32_t>(outChannels);
            img->arrayLayers= 1;
            img->isCubeMap  = false;
            img->isCompressed = false;

            const size_t pixelSize = isHDR ? sizeof(float) : sizeof(uint8_t);
            const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h)
                                   * static_cast<size_t>(outChannels) * pixelSize;

            ImageMipLevel mip;
            mip.width  = img->width;
            mip.height = img->height;
            mip.rowPitch = static_cast<uint32_t>(static_cast<size_t>(w) * outChannels * pixelSize);
            mip.pixels.bytes.resize(byteCount);
            if (byteCount > 0)
                std::memcpy(mip.pixels.bytes.data(), pixels, byteCount);

            img->mips.push_back(std::move(mip));
            stbi_image_free(pixels);
            return img;
        }

        std::shared_ptr<ImageAssetData> LoadDds(const std::string& path,
                                                const ImageLoadOptions& opts)
        {
            gli::texture tex = gli::load(path);
            if (tex.empty())
            {
                LOG_ERROR("AssetLoader::ImageLoader",
                          "gli load failed: %s",
                          path.c_str());
                return nullptr;
            }

            auto img = std::make_shared<ImageAssetData>();
            img->sourcePath = path;
            img->pixelType  = ImagePixelType::Compressed;
            img->colorSpace = opts.isSRGBHint ? ImageColorSpace::SRGB : ImageColorSpace::Linear;
            img->width      = static_cast<uint32_t>(tex.extent().x);
            img->height     = static_cast<uint32_t>(tex.extent().y);
            img->channels   = 4;
            img->arrayLayers= 1;
            img->isCubeMap  = (tex.faces() > 1);
            img->isCompressed = true;
            img->compressedFormatToken = static_cast<uint32_t>(tex.format());

            // 仅暴露 mip0 字节流（保持与现有 GLUtils::ConfigureDDTexture 对齐）；
            // 多 mip 解析由后续后端按需扩展。
            ImageMipLevel mip;
            mip.width    = img->width;
            mip.height   = img->height;
            mip.rowPitch = 0;
            const size_t byteCount = tex.size();
            mip.pixels.bytes.resize(byteCount);
            if (byteCount > 0)
                std::memcpy(mip.pixels.bytes.data(), tex.data(), byteCount);
            img->mips.push_back(std::move(mip));
            return img;
        }
    } // anonymous

    std::shared_ptr<ImageAssetData>
    LoadImage2D(const std::string& path, const ImageLoadOptions& opts)
    {
        const std::string ext = GetExtensionLower(path);
        if (ext == "dds" || ext == "ktx")
            return LoadDds(path, opts);
        if (ext == "hdr")
            return LoadStbi(path, opts, /*isHDR*/true);
        return LoadStbi(path, opts, /*isHDR*/false);
    }

    std::shared_ptr<ImageAssetData>
    LoadImageCube(const std::string facePaths[6], const ImageLoadOptions& opts)
    {
        // 六张面统一以非 HDR 8bit 路径处理（与 GLUtils::LoadCubeTextureFromFile 对齐）
        std::shared_ptr<ImageAssetData> cube;
        for (int face = 0; face < 6; ++face)
        {
            auto faceImg = LoadStbi(facePaths[face], opts, /*isHDR*/false);
            if (!faceImg) return nullptr;

            if (face == 0)
            {
                cube = std::make_shared<ImageAssetData>(*faceImg);
                cube->isCubeMap   = true;
                cube->arrayLayers = 6;
                cube->mips.clear();
            }
            // 取每张面 mip0 加入 mips（用 mip 数组承载 6 面）
            cube->mips.push_back(std::move(faceImg->mips.front()));
        }
        return cube;
    }
} // namespace TitusAsset
