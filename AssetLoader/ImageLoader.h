#pragma once
// ============================================================================
// AssetLoader - ImageLoader.h
// 解码 PNG/JPG/HDR/DDS 等图像文件为纯 CPU 像素数据（ImageAssetData）。
// 不进行任何 GPU 上传操作。
// ============================================================================
#include <memory>
#include <string>

#include "AssetTypes.h"

namespace TitusAsset
{
    struct ImageLoadOptions
    {
        bool flipVerticallyOnLoad = true;   // 与 stb_image_set_flip_vertically_on_load 对齐
        bool isSRGBHint           = false;  // 是否按 sRGB 解释
        // 期望通道数（0 表示按文件原通道）
        int  desiredChannels      = 0;
    };

    // 加载 2D 纹理（PNG/JPG/HDR/DDS/KTX 等）。
    // 失败返回 nullptr。
    std::shared_ptr<ImageAssetData>
    LoadImage2D(const std::string& path, const ImageLoadOptions& opts = {});

    // 加载 6 张面构成的 CubeMap。order: +X,-X,+Y,-Y,+Z,-Z
    std::shared_ptr<ImageAssetData>
    LoadImageCube(const std::string facePaths[6], const ImageLoadOptions& opts = {});
} // namespace TitusAsset
