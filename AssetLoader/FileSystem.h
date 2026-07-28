#pragma once
// ============================================================================
// AssetLoader - FileSystem.h
// 通用文件系统工具：读取整文件 / 路径切分。
// ============================================================================
#include <string>
#include <vector>
#include <cstdint>

namespace TitusAsset
{
    // 读取文件全部字节；失败返回 false。
    bool ReadAllBytes (const std::string& path, std::vector<uint8_t>& outBytes);
    bool ReadAllText  (const std::string& path, std::string& outText);

    // 取文件路径的目录部分（含末尾分隔符）。
    std::string GetDirectory (const std::string& path);
    // 取文件名（不含目录、含扩展名）。
    std::string GetFileName  (const std::string& path);
    // 取扩展名（不含点；若无扩展名返回空），全部小写返回。
    std::string GetExtensionLower(const std::string& path);
    // 拼接（自动补斜杠，识别 / 与 \）。
    std::string JoinPath     (const std::string& dir, const std::string& sub);
} // namespace TitusAsset
