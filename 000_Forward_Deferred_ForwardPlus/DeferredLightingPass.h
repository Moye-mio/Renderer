#pragma once
// ============================================================================
// 000_Forward_Deferred_ForwardPlus - DeferredLightingPass
//
// 延迟渲染的光照 Pass：一个全屏三角形 Pass，从共享数据黑板取 G-Buffer 的
// Albedo / Normal(view) / Position(view) 三张纹理，对 N（默认 1000）个点光源做
// Blinn-Phong 累加，直接把结果输出到默认 backbuffer。
//
// 光源与 BRDF 常数来自 TechniqueContext::shared；debug 视图来自
// TechniqueContext::deferred。mode != Deferred 时 Record 早退。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

struct TechniqueContext;

class DeferredLightingPass : public TitusRHI::IRenderPass
{
public:
    DeferredLightingPass();
    ~DeferredLightingPass() override = default;

    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    TechniqueContext* m_ctx = nullptr;

    TitusRHI::SamplerHandle m_sampler;
    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;
    TitusRHI::BufferHandle m_lightUbo;
};
