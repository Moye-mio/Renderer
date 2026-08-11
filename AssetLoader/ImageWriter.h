#pragma once
// ============================================================================
// AssetLoader - ImageWriter.h
// 将 CPU 侧 RGBA8 像素写出为 PNG（stb_image_write）。
// ============================================================================
#include <cstdint>
#include <string>

namespace TitusAsset
{
    // 写出紧凑 RGBA8（每像素 4 字节，行主序，第 0 行为图像顶部）。
    // 成功返回 true；失败打日志并返回 false。
    bool SaveImage2DPNG(const std::string& path,
                        uint32_t width,
                        uint32_t height,
                        const uint8_t* rgba);
} // namespace TitusAsset
