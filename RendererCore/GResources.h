#pragma once
// ============================================================================
// RendererCore - GResources
// 统一资源对象基类：
//   - RHIBuffer  ：buffer 资源对象；持 BufferDesc + BufferHandle
//   - RHITexture ：render surface（color/depth）；持 desc + handle
//   - RHIShader  ：shader 程序；持 ShaderDesc + ShaderHandle + reflection 占位
// 用途：
//   1) 给 Material 层提供"通过句柄反查资源元数据"的能力。
//   2) 给延迟销毁队列提供更类型安全的"待删除条目"形式（虽然目前队列仍以
//      kind+id 表示，但子类可以在自己的查询表里持有这些基类指针）。
// 注意：本基类**不**含任何后端原生类型字段（GLuint/VkBuffer 等）；后端子类
// 在自己的 Entry 表中扩展（GLBufferEntry 已持 GLuint）。
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "GHandle.h"
#include "GDescs.h"

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // RHIBuffer —— 通用 buffer 基类
    // ------------------------------------------------------------------------
    class RHIBuffer
    {
    public:
        RHIBuffer() = default;
        RHIBuffer(BufferHandle h, const BufferDesc& d) : m_handle(h), m_desc(d) {}
        virtual ~RHIBuffer() = default;

        BufferHandle      GetHandle() const { return m_handle; }
        const BufferDesc& GetDesc()   const { return m_desc; }
        size_t            GetSize()   const { return m_desc.size; }

    protected:
        BufferHandle m_handle{};
        BufferDesc   m_desc{};
    };

    // ------------------------------------------------------------------------
    // RHITexture —— color / depth surface 基类
    // ------------------------------------------------------------------------
    class RHITexture
    {
    public:
        RHITexture() = default;
        RHITexture(TextureHandle h, const TextureDesc& d) : m_handle(h), m_desc(d) {}
        virtual ~RHITexture() = default;

        TextureHandle      GetHandle() const { return m_handle; }
        const TextureDesc& GetDesc()   const { return m_desc; }
        uint32_t           GetWidth()  const { return m_desc.width;  }
        uint32_t           GetHeight() const { return m_desc.height; }

    protected:
        TextureHandle m_handle{};
        TextureDesc   m_desc{};
    };

    // ------------------------------------------------------------------------
    // RHIShader —— shader 程序基类（持 reflection 占位）
    // ------------------------------------------------------------------------
    struct ShaderParameterMap
    {
        // 先给最小占位；后续用 spirv-cross 反射填满。
        struct UniformEntry { std::string name; uint32_t set = 0; uint32_t binding = 0; uint32_t size = 0; };
        struct TextureEntry { std::string name; uint32_t set = 0; uint32_t binding = 0; };

        std::vector<UniformEntry> uniformBlocks;
        std::vector<TextureEntry> textureBindings;
    };

    class RHIShader
    {
    public:
        RHIShader() = default;
        RHIShader(ShaderHandle h, const ShaderDesc& d) : m_handle(h), m_desc(d) {}
        virtual ~RHIShader() = default;

        ShaderHandle              GetHandle()     const { return m_handle; }
        const ShaderDesc&         GetDesc()       const { return m_desc; }
        const ShaderParameterMap& GetReflection() const { return m_reflection; }

        // 子类 / Material 层可在创建期写入反射结构
        void SetReflection(ShaderParameterMap&& refl) { m_reflection = std::move(refl); }

    protected:
        ShaderHandle       m_handle{};
        ShaderDesc         m_desc{};
        ShaderParameterMap m_reflection{};
    };
}
