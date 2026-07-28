#pragma once
// ============================================================================
// RendererCore - Material
// 跨后端材质对象（任务 9 / M-A 最小可用版）：
//   - 持有 ShaderAsset 指针 + GraphicsPipelineDesc 模板 + ShaderParameterSet
//   - GetOrCreatePipeline(device) 按当前 desc 走 device.CreatePipeline 状态缓存（任务 8）
//   - Apply(device, cmd) 把属性表中的 uniform / texture 写入到对应绑定点（M-A 阶段
//     仅记录到 cmd 的 SetUniform/SetTexture 命令；任务 13 接入 spirv-cross 后会用
//     反射信息自动定位 set/binding）
//
// 设计参考：requirements.md 需求 18-MA / 16.3 / 9.x。
// ============================================================================
#include <memory>
#include <string>

#include "GHandle.h"
#include "GDescs.h"
#include "ShaderAsset.h"
#include "ShaderParameterSet.h"

namespace TitusRHI
{
    class IGDevice;
    class RenderCommandList;

    class Material
    {
    public:
        Material() = default;
        ~Material() = default;

        // 关联 shader 资产（不持有所有权；调用者负责 ShaderAsset 的生命周期）
        void SetShader(ShaderAsset* shader) { m_shader = shader; m_pipelineDirty = true; }
        ShaderAsset* GetShader() const { return m_shader; }

        // 配置 pipeline 模板（顶点布局 / 光栅 / 深度 / 混合 / RT 布局）
        // 上层一般在 Material 创建后立即调用一次；之后改属性走 SetFloat 等。
        void SetPipelineDesc(const GraphicsPipelineDesc& desc)
        {
            m_pipelineTemplate = desc;
            m_pipelineDirty    = true;
        }
        const GraphicsPipelineDesc& GetPipelineDesc() const { return m_pipelineTemplate; }

        // —— 属性表 Setter（转发到 m_properties） ——
        void SetFloat (const std::string& n, float v)               { m_properties.SetFloat(n, v); }
        void SetVector(const std::string& n, const ShaderVec4& v)   { m_properties.SetVector(n, v); }
        void SetMatrix(const std::string& n, const ShaderMat4& m)   { m_properties.SetMatrix(n, m); }
        void SetTexture(const std::string& n, TextureHandle h, SamplerHandle s = {})
        {
            m_properties.SetTexture(n, h, s);
        }

        const ShaderParameterSet& GetProperties() const { return m_properties; }
        ShaderParameterSet&       MutableProperties()   { return m_properties; }

        // 解析当前 shader 与属性表，确保 PipelineHandle 已就绪。
        // 内部会把 m_shader->GetVS()/GetFS() 写入 m_pipelineTemplate 后再 CreatePipeline。
        // 由于 CreatePipeline 走状态缓存（任务 8），重复调用零开销。
        bool GetOrCreatePipeline(IGDevice& device);

        // 一键绑定到命令列表：BindPipeline + 把 m_properties 与 device.GContextData
        // 的 globalProperties 合并后写入对应 binding 点。
        // M-A 阶段：仅 BindPipeline；属性 Apply 留给业务侧逐项 cmd.SetUniform/SetTexture
        // （或在任务 13 反射就绪后自动）
        void Bind(IGDevice& device, RenderCommandList& cmd);

        // 释放（不释放 shader——shader 由调用者管理）
        void Reset()
        {
            m_pipeline      = {};
            m_pipelineDirty = true;
            m_properties.Clear();
        }

    private:
        ShaderAsset*         m_shader        = nullptr;
        GraphicsPipelineDesc m_pipelineTemplate{};
        PipelineHandle       m_pipeline{};
        bool                 m_pipelineDirty = true;
        ShaderParameterSet   m_properties;
    };
}