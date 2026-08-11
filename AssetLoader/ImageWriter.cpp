// ============================================================================
// AssetLoader - ImageWriter.cpp
// PNG 写出：stb_image_write（IMPLEMENTATION 仅在本 TU）。
// ============================================================================
#include "ImageWriter.h"

#include "Logger.h"

#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace TitusAsset
{
    bool SaveImage2DPNG(const std::string& path,
                        uint32_t width,
                        uint32_t height,
                        const uint8_t* rgba)
    {
        if (path.empty() || !rgba || width == 0 || height == 0)
        {
            LOG_ERROR("AssetLoader::ImageWriter",
                      "SaveImage2DPNG: invalid args path='%s' %ux%u rgba=%p",
                      path.c_str(), width, height, static_cast<const void*>(rgba));
            return false;
        }

        namespace fs = std::filesystem;
        try
        {
            fs::path p(path);
            if (p.has_parent_path())
            {
                std::error_code ec;
                fs::create_directories(p.parent_path(), ec);
            }
        }
        catch (...)
        {
            LOG_ERROR("AssetLoader::ImageWriter",
                      "SaveImage2DPNG: create_directories failed for '%s'",
                      path.c_str());
            return false;
        }

        const int stride = static_cast<int>(width) * 4;
        const int ok = stbi_write_png(path.c_str(),
                                      static_cast<int>(width),
                                      static_cast<int>(height),
                                      4,
                                      rgba,
                                      stride);
        if (!ok)
        {
            LOG_ERROR("AssetLoader::ImageWriter",
                      "stbi_write_png failed: %s", path.c_str());
            return false;
        }

        LOG_INFO("AssetLoader::ImageWriter", "Wrote PNG: %s (%ux%u)",
                 path.c_str(), width, height);
        return true;
    }
} // namespace TitusAsset
