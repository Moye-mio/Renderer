// ============================================================================
// RendererCore - Material.cpp
// ============================================================================
#include "Material.h"
#include "IGDevice.h"
#include "RenderCommandList.h"

#include <iostream>
#include "Logger.h"
#include <unordered_map>

namespace TitusRHI
{
    bool Material::GetOrCreatePipeline(IGDevice& device)
    {
        if (!m_shader || !m_shader->IsValid())
        {
        LOG_STREAM_ERROR("Material") << "GetOrCreatePipeline: shader not loaded";
            return false;
        }
        if (m_pipeline.IsValid() && !m_pipelineDirty) return true;

        // 把 shader handle 写入 pipeline 模板 → 状态缓存命中或新建
        m_pipelineTemplate.vertexShader   = m_shader->GetVS();
        m_pipelineTemplate.fragmentShader = m_shader->GetFS();
        m_pipeline      = device.CreatePipeline(m_pipelineTemplate);
        m_pipelineDirty = false;
        return m_pipeline.IsValid();
    }

    void Material::Bind(IGDevice& device, RenderCommandList& cmd)
    {
        if (!GetOrCreatePipeline(device))
        {
        LOG_STREAM_ERROR("Material") << "Bind: pipeline unavailable";
            return;
        }
        cmd.BindPipeline(m_pipeline);

        // ----------------------------------------------------------------
        // 基于 reflection 自动绑定 Texture / Sampler。
        // 步骤：
        //   1) 取出 shader 反射出的 binding 列表
        //   2) 按 set 分桶，每桶组装一个 ResourceSetDesc
        //   3) 对每个 Texture/Sampler 类型 binding，按 name 在 m_properties
        //      中查 TexBinding；缺省值跳过
        //   4) 调 cmd.BindResourceSet(setIdx, desc)
        // 注：UBO 的"自动 staging 上传"留待后续——目前 UBO 由上层手动构造
        // ResourceSetDesc 后调 BindResourceSet 即可。
        // ----------------------------------------------------------------
        if (!m_shader) return;
        const auto& refl = m_shader->GetReflection();
        if (refl.bindings.empty()) return;

        // set → ResourceSetDesc
        std::unordered_map<uint32_t, ResourceSetDesc> setBuckets;

        for (const auto& rb : refl.bindings)
        {
            if (rb.type != ResourceBindingType::CombinedImageSampler &&
                rb.type != ResourceBindingType::SampledTexture       &&
                rb.type != ResourceBindingType::Sampler)
            {
                continue;   // UBO/SSBO/StorageImage 暂不自动处理
            }

            ShaderParameterSet::TexBinding tex{};
            if (!m_properties.TryGetTexture(rb.name, tex))
                continue;   // 该 binding 业务侧未设置，跳过；后端将沿用上次绑定

            ResourceBindingValue rbv;
            rbv.binding = rb.binding;
            rbv.type    = rb.type;
            rbv.texture = tex.texture;
            rbv.sampler = tex.sampler;
            setBuckets[rb.set].bindings.push_back(rbv);
        }

        for (auto& [setIdx, desc] : setBuckets)
        {
            if (!desc.bindings.empty())
                cmd.BindResourceSet(setIdx, desc);
        }
    }
}
