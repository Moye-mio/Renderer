#pragma once
// ============================================================================
// AssetLoader - AssetTypes.h
//
// 纯 CPU 资源中间表示（IR）。完全独立于任何 GPU API：
//   - 不 include <gl/...>、<vulkan/...>、Renderer*/RendererCore/*
//   - 顶点格式只描述"语义 + 类型"，不绑定 OpenGL/Vulkan 的内部 enum
//   - Texture 像素数据使用 uint8_t/float 原始字节，由后续 GPU 后端按需上传
//
// 设计参考：通用资源包（AssetBundle）的 BinaryAsset / Mesh / Texture2D
// 数据布局 + Renderer/Mesh.h 的现有字段。
// 任务 11（M5-A）：requirements.md 11.x、task-item.md M5-A。
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "TitusMath.h"

namespace TitusAsset
{
    // -----------------------------------------------------------------------
    // 通用：原始字节缓冲（拥有内存所有权）
    // -----------------------------------------------------------------------
    struct ByteBuffer
    {
        std::vector<uint8_t> bytes;

        bool   empty() const noexcept { return bytes.empty(); }
        size_t size () const noexcept { return bytes.size(); }
        const  uint8_t* data() const noexcept { return bytes.data(); }
        uint8_t*        data()       noexcept { return bytes.data(); }
    };

    // -----------------------------------------------------------------------
    // Image / Texture IR
    // -----------------------------------------------------------------------
    enum class ImagePixelType : uint8_t
    {
        UNorm8,     // stb 默认：每通道 uint8
        Float16,    // hdr 半精度（暂不直接生成，预留）
        Float32,    // hdr 32f
        Compressed  // dds/ktx：原始压缩块，由 channels=blockSize 标识
    };

    enum class ImageColorSpace : uint8_t
    {
        Linear,
        SRGB
    };

    struct ImageMipLevel
    {
        uint32_t   width  = 0;
        uint32_t   height = 0;
        uint32_t   rowPitch = 0;   // 仅压缩格式有意义
        ByteBuffer pixels;
    };

    struct ImageAssetData
    {
        std::string      sourcePath;       // 来源文件路径（用于诊断）
        ImagePixelType   pixelType   = ImagePixelType::UNorm8;
        ImageColorSpace  colorSpace  = ImageColorSpace::Linear;
        uint32_t         width       = 0;
        uint32_t         height      = 0;
        uint32_t         channels    = 0;  // 1/2/3/4；压缩纹理含义另定
        uint32_t         arrayLayers = 1;  // CubeMap=6
        bool             isCubeMap   = false;
        bool             isCompressed = false;
        // 内部格式标识（仅 dds/ktx 使用）；后端按 GFormat 翻译
        uint32_t         compressedFormatToken = 0;
        std::vector<ImageMipLevel> mips;   // 至少 1 级
    };

    // -----------------------------------------------------------------------
    // Mesh / Vertex IR
    // -----------------------------------------------------------------------
    struct MeshVertex
    {
        TitusMath::Vec3 position{0.0f};
        TitusMath::Vec3 normal  {0.0f, 0.0f, 1.0f};
        TitusMath::Vec2 uv      {0.0f};
        TitusMath::Vec3 tangent {1.0f, 0.0f, 0.0f};
        TitusMath::Vec3 bitangent{0.0f, 1.0f, 0.0f};
    };

    enum class TextureSlot : uint8_t
    {
        Diffuse   = 0,
        Specular  = 1,
        Normal    = 2,
        Roughness = 3,
        Metallic  = 4,
        Ambient   = 5,
        Height    = 6,
        Emissive  = 7,
        SlotCount
    };

    struct MaterialTextureRef
    {
        TextureSlot slot   = TextureSlot::Diffuse;
        std::string path;          // 相对 / 绝对路径，由 ModelLoader 拼接
        bool        isSRGB = false;
    };

    struct MaterialAssetData
    {
        std::string name;
        TitusMath::Vec3 ambientColor   {0.1f};
        TitusMath::Vec3 diffuseColor   {1.0f};
        TitusMath::Vec3 specularColor  {1.0f};
        float       shininess        = 32.0f;
        float       refractiveIndex  = 1.0f;
        std::vector<MaterialTextureRef> textures;
    };

    struct MeshAssetData
    {
        std::string                 name;
        std::vector<MeshVertex>     vertices;
        std::vector<uint32_t>       indices;
        MaterialAssetData           material;
        // CPU 端 AABB，后续 GPU 上传时无需重新计算
        TitusMath::Vec3             aabbMin{0.0f};
        TitusMath::Vec3             aabbMax{0.0f};
    };

    // -----------------------------------------------------------------------
    // Model = 多 Mesh 集合 + 共享纹理表
    // -----------------------------------------------------------------------
    struct ModelAssetData
    {
        std::string                                      sourcePath;
        std::string                                      directory;   // 用于解析相对纹理路径
        std::vector<MeshAssetData>                       meshes;
        // 模型级共享纹理（同一 path 仅出现一次），由 ModelLoader 去重填充
        std::vector<std::shared_ptr<ImageAssetData>>     sharedImages;
    };
} // namespace TitusAsset
