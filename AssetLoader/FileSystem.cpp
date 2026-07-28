// ============================================================================
// AssetLoader - FileSystem.cpp
// ============================================================================
#include "FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace TitusAsset
{
    bool ReadAllBytes(const std::string& path, std::vector<uint8_t>& outBytes)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
            return false;
        const std::streamsize sz = f.tellg();
        if (sz < 0)
            return false;
        outBytes.resize(static_cast<size_t>(sz));
        f.seekg(0, std::ios::beg);
        if (sz > 0)
            f.read(reinterpret_cast<char*>(outBytes.data()), sz);
        return f.good() || f.eof();
    }

    bool ReadAllText(const std::string& path, std::string& outText)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        std::stringstream ss;
        ss << f.rdbuf();
        outText = ss.str();
        return true;
    }

    static size_t FindLastSep(const std::string& path)
    {
        const size_t a = path.find_last_of('/');
        const size_t b = path.find_last_of('\\');
        if (a == std::string::npos) return b;
        if (b == std::string::npos) return a;
        return (a > b) ? a : b;
    }

    std::string GetDirectory(const std::string& path)
    {
        const size_t pos = FindLastSep(path);
        if (pos == std::string::npos) return std::string();
        return path.substr(0, pos + 1);
    }

    std::string GetFileName(const std::string& path)
    {
        const size_t pos = FindLastSep(path);
        if (pos == std::string::npos) return path;
        return path.substr(pos + 1);
    }

    std::string GetExtensionLower(const std::string& path)
    {
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return std::string();
        // 防止 ".." / "/foo" 这种无扩展名的情况
        const size_t sep = FindLastSep(path);
        if (sep != std::string::npos && dot < sep) return std::string();

        std::string ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    }

    std::string JoinPath(const std::string& dir, const std::string& sub)
    {
        if (dir.empty()) return sub;
        if (sub.empty()) return dir;
        const char last = dir.back();
        if (last == '/' || last == '\\')
            return dir + sub;
        return dir + "/" + sub;
    }
} // namespace TitusAsset
