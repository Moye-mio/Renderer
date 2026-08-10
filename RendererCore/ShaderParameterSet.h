#pragma once
// ============================================================================
// RendererCore - ShaderParameterSet
// shader 参数集：运行期 SetFloat / SetVector / SetMatrix / SetTexture，
// 绘制时由 Material::Apply 取出与 ShaderParameterMap 对齐后一次性写入 GPU。
// 本文件仅是"键值容器"，不与具体后端绑定；GContextData 持有一份默认全局表，
// Material 层会在 Apply 阶段合并自身覆盖项后提交。
// ============================================================================
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

#include "TitusMath.h"

#include "GHandle.h"

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // 4 / 16 字节属性值容器（与 TitusMath 统一）
    // ------------------------------------------------------------------------
    using ShaderVec4 = TitusMath::Vec4;
    using ShaderMat4 = TitusMath::Mat4;

    class ShaderParameterSet
    {
    public:
        // -- Setter --
        void SetFloat (const std::string& name, float v)
        {
            m_floats[name] = v;
            ++m_dirty;
        }
        void SetVector(const std::string& name, const ShaderVec4& v)
        {
            m_vectors[name] = v;
            ++m_dirty;
        }
        void SetMatrix(const std::string& name, const ShaderMat4& m)
        {
            m_matrices[name] = m;
            ++m_dirty;
        }
        void SetTexture(const std::string& name, TextureHandle h, SamplerHandle s = {})
        {
            m_textures[name] = TexBinding{ h, s };
            ++m_dirty;
        }

        // -- Getter（绑定阶段使用）--
        bool TryGetFloat (const std::string& name, float& out) const
        {
            auto it = m_floats.find(name);
            if (it == m_floats.end()) return false; out = it->second; return true;
        }
        bool TryGetVector(const std::string& name, ShaderVec4& out) const
        {
            auto it = m_vectors.find(name);
            if (it == m_vectors.end()) return false; out = it->second; return true;
        }
        bool TryGetMatrix(const std::string& name, ShaderMat4& out) const
        {
            auto it = m_matrices.find(name);
            if (it == m_matrices.end()) return false; out = it->second; return true;
        }

        struct TexBinding { TextureHandle texture{}; SamplerHandle sampler{}; };
        bool TryGetTexture(const std::string& name, TexBinding& out) const
        {
            auto it = m_textures.find(name);
            if (it == m_textures.end()) return false; out = it->second; return true;
        }

        // 合并：把 other 的所有键值复制覆盖到自身。
        void Merge(const ShaderParameterSet& other)
        {
            for (auto& [k, v] : other.m_floats)   m_floats[k]   = v;
            for (auto& [k, v] : other.m_vectors)  m_vectors[k]  = v;
            for (auto& [k, v] : other.m_matrices) m_matrices[k] = v;
            for (auto& [k, v] : other.m_textures) m_textures[k] = v;
            ++m_dirty;
        }

        void Clear()
        {
            m_floats.clear();
            m_vectors.clear();
            m_matrices.clear();
            m_textures.clear();
            ++m_dirty;
        }

        uint32_t GetDirtyCounter() const { return m_dirty; }

        // 直接迭代访问（Material::Apply 用）
        const std::unordered_map<std::string, float>&         Floats()   const { return m_floats; }
        const std::unordered_map<std::string, ShaderVec4>&    Vectors()  const { return m_vectors; }
        const std::unordered_map<std::string, ShaderMat4>&    Matrices() const { return m_matrices; }
        const std::unordered_map<std::string, TexBinding>&    Textures() const { return m_textures; }

    private:
        std::unordered_map<std::string, float>      m_floats;
        std::unordered_map<std::string, ShaderVec4> m_vectors;
        std::unordered_map<std::string, ShaderMat4> m_matrices;
        std::unordered_map<std::string, TexBinding> m_textures;
        uint32_t                                    m_dirty = 0;
    };
}
