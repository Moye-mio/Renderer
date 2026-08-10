// ============================================================================
// RendererCore - ShaderReflector.cpp
// 3 条反射路径的实现。SPIRV-Cross 真反射目前以 #if 守卫；
// 未开启宏时回落到 GLSL 源码扫描或 Hints。
// ============================================================================
#include "ShaderReflector.h"

#include <iostream>
#include "Logger.h"
#include <regex>
#include <algorithm>
#include <unordered_map>
#include <utility>

#if defined(TITUS_ENABLE_SPIRV_CROSS)
    // 用户在 vcxproj/cmake 里加 SPIRV-Cross 的 include 后，这一段才会被编译
    #include <spirv_cross/spirv_cross.hpp>
    #include <spirv_cross/spirv_glsl.hpp>
#endif

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // 内部辅助：把 (set,binding) → bindings 数组下标 的映射构建出来
    // ------------------------------------------------------------------------
    namespace
    {
        struct BindingKey
        {
            uint32_t set;
            uint32_t binding;
            bool operator==(const BindingKey& o) const noexcept
            { return set == o.set && binding == o.binding; }
        };
        struct BindingKeyHash
        {
            size_t operator()(const BindingKey& k) const noexcept
            { return (static_cast<size_t>(k.set) << 16) ^ k.binding; }
        };

        using BindingIndex = std::unordered_map<BindingKey, size_t, BindingKeyHash>;

        BindingIndex BuildIndex(std::vector<ResourceBinding>& bindings)
        {
            BindingIndex idx;
            idx.reserve(bindings.size());
            for (size_t i = 0; i < bindings.size(); ++i)
                idx[{bindings[i].set, bindings[i].binding}] = i;
            return idx;
        }

        // append 一条 binding；若 (set,binding) 已存在则按位或 stages
        void UpsertBinding(std::vector<ResourceBinding>& dst,
                           BindingIndex&                 idx,
                           ResourceBinding               b)
        {
            auto it = idx.find({b.set, b.binding});
            if (it == idx.end())
            {
                idx[{b.set, b.binding}] = dst.size();
                dst.push_back(std::move(b));
            }
            else
            {
                auto& existed = dst[it->second];
                existed.stages = existed.stages | b.stages;
            }
        }
    } // anonymous namespace

    // ========================================================================
    // 1) SPIR-V
    // ========================================================================
    bool ShaderReflector::ReflectFromSPIRV(const uint32_t* spv,
                                           size_t          wordCount,
                                           ShaderStage     stage,
                                           ReflectionInfo& out)
    {
    #if defined(TITUS_ENABLE_SPIRV_CROSS)
        if (!spv || wordCount == 0)
        {
        LOG_STREAM_ERROR("ShaderReflector") << "empty SPIR-V";
            return false;
        }
        try
        {
            spirv_cross::Compiler comp(spv, wordCount);
            const auto resources = comp.get_shader_resources();

            BindingIndex idx = BuildIndex(out.bindings);

            auto pushBinding = [&](const spirv_cross::Resource& r,
                                   ResourceBindingType          type)
            {
                ResourceBinding b;
                b.name    = r.name;
                b.set     = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
                b.binding = comp.get_decoration(r.id, spv::DecorationBinding);
                b.count   = 1;
                b.type    = type;
                b.stages  = stage;
                UpsertBinding(out.bindings, idx, std::move(b));
            };

            for (auto& r : resources.uniform_buffers)
                pushBinding(r, ResourceBindingType::UniformBuffer);
            for (auto& r : resources.storage_buffers)
                pushBinding(r, ResourceBindingType::StorageBuffer);
            for (auto& r : resources.sampled_images)
                pushBinding(r, ResourceBindingType::CombinedImageSampler);
            for (auto& r : resources.separate_images)
                pushBinding(r, ResourceBindingType::SampledTexture);
            for (auto& r : resources.separate_samplers)
                pushBinding(r, ResourceBindingType::Sampler);
            for (auto& r : resources.storage_images)
                pushBinding(r, ResourceBindingType::StorageTexture);

            // push_constant 范围
            for (auto& r : resources.push_constant_buffers)
            {
                const auto ranges = comp.get_active_buffer_ranges(r.id);
                for (auto& rg : ranges)
                {
                    PushConstantRange pc;
                    pc.stages = stage;
                    pc.offset = static_cast<uint32_t>(rg.offset);
                    pc.size   = static_cast<uint32_t>(rg.range);
                    out.pushConstants.push_back(pc);
                }
            }
            return true;
        }
        catch (const std::exception& e)
        {
        LOG_STREAM_ERROR("ShaderReflector") << "SPIRV-Cross exception: " << e.what();
            return false;
        }
    #else
        (void)spv; (void)wordCount; (void)stage; (void)out;
        // 未开启宏：返回 false，调用方应回退到 ReflectFromGLSLSource / ReflectFromHints
        return false;
    #endif
    }

    // ========================================================================
    // 2) GLSL 源码（正则扫描；当 SPIRV-Cross 不可用时的回退）
    //    支持的声明（足够覆盖项目内大多数 #version 330+ shader）：
    //      layout(binding = N) uniform sampler2D u_xxx;
    //      uniform sampler2D u_xxx;                    // 无 binding 时按出现顺序赋值
    //      layout(set = S, binding = N) uniform Block { ... } u_yyy;
    //      layout(binding = N) uniform Block { ... };
    //      uniform Block { ... };
    // ========================================================================
    bool ShaderReflector::ReflectFromGLSLSource(const std::string& source,
                                                ShaderStage        stage,
                                                ReflectionInfo&    out)
    {
        if (source.empty()) return false;
        BindingIndex idx = BuildIndex(out.bindings);

        // 用一个递增计数器为没有显式 binding 的 sampler 分配 binding 槽
        uint32_t autoSamplerBinding = 0;
        for (auto& b : out.bindings)
        {
            if (b.type == ResourceBindingType::CombinedImageSampler ||
                b.type == ResourceBindingType::SampledTexture)
                autoSamplerBinding = std::max(autoSamplerBinding, b.binding + 1);
        }

        // ---- sampler*: layout(binding=N) uniform samplerXxx name; ----
        // 简化处理：只识别 sampler2D / samplerCube / sampler2DArray / sampler3D
        // 注意：不使用 std::regex::multiline（部分 MSVC 版本不支持），
        // 改为用 (^|\n|\r) 显式匹配行首，避免依赖 multiline 标志。
        static const std::regex kReSampler(
            R"((?:^|[\r\n;])(?:\s*layout\s*\(\s*(?:set\s*=\s*(\d+)\s*,\s*)?binding\s*=\s*(\d+)\s*\)\s*)?\s*uniform\s+(sampler2D|samplerCube|sampler2DArray|sampler3D|sampler2DShadow|samplerCubeShadow)\s+(\w+)\s*;)");
        for (std::sregex_iterator it(source.begin(), source.end(), kReSampler), end; it != end; ++it)
        {
            const auto& m = *it;
            ResourceBinding b;
            b.name    = m[4].str();
            b.set     = m[1].matched ? static_cast<uint32_t>(std::stoul(m[1].str())) : 0u;
            if (m[2].matched)
                b.binding = static_cast<uint32_t>(std::stoul(m[2].str()));
            else
                b.binding = autoSamplerBinding++;
            b.count   = 1;
            b.type    = ResourceBindingType::CombinedImageSampler;
            b.stages  = stage;
            UpsertBinding(out.bindings, idx, std::move(b));
        }

        // ---- UBO: layout(binding=N) uniform Block { ... } [name]; ----
        // 该正则不依赖行首锚点，无需 multiline 标志
        static const std::regex kReUbo(
            R"(layout\s*\(\s*(?:set\s*=\s*(\d+)\s*,\s*)?(?:std140\s*,\s*)?binding\s*=\s*(\d+)\s*\)\s*uniform\s+(\w+)\s*\{[^}]*\}\s*(\w+)?\s*;)");
        for (std::sregex_iterator it(source.begin(), source.end(), kReUbo), end; it != end; ++it)
        {
            const auto& m = *it;
            ResourceBinding b;
            b.set     = m[1].matched ? static_cast<uint32_t>(std::stoul(m[1].str())) : 0u;
            b.binding = static_cast<uint32_t>(std::stoul(m[2].str()));
            b.count   = 1;
            b.type    = ResourceBindingType::UniformBuffer;
            b.stages  = stage;
            // 名字优先用实例名（m[4]），否则用 block 名
            b.name    = m[4].matched && !m[4].str().empty() ? m[4].str() : m[3].str();
            UpsertBinding(out.bindings, idx, std::move(b));
        }
        return true;
    }

    // ========================================================================
    // 3) Hints
    // ========================================================================
    void ShaderReflector::ReflectFromHints(const std::vector<ResourceBinding>& hints,
                                           ShaderStage                          stage,
                                           ReflectionInfo&                      out)
    {
        BindingIndex idx = BuildIndex(out.bindings);
        for (const auto& src : hints)
        {
            ResourceBinding b = src;
            b.stages = b.stages | stage;
            UpsertBinding(out.bindings, idx, std::move(b));
        }
    }

    // ========================================================================
    // 工具：合并
    // ========================================================================
    void ShaderReflector::Merge(ReflectionInfo& dst, const ReflectionInfo& src)
    {
        BindingIndex idx = BuildIndex(dst.bindings);
        for (const auto& b : src.bindings)
            UpsertBinding(dst.bindings, idx, b);
        for (const auto& pc : src.pushConstants)
            dst.pushConstants.push_back(pc);
    }
} // namespace TitusRHI
