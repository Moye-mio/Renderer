#pragma once
// ============================================================================
// 000_Deferred_Shading - ForwardShadingPass
//
// 前向着色：把 Sponza 直接画到默认 backbuffer，片元里对 shared 点光做与
// Deferred 同一套视空间 Blinn-Phong。mode != Forward 时 Record 早退。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class ForwardShadingPass : public TitusRHI::IRenderPass
{
public:
    ForwardShadingPass();
    ~ForwardShadingPass() override = default;

    void SetSponza(Sponza* sponza) { m_sponza = sponza; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    Sponza* m_sponza = nullptr;
    TechniqueContext* m_ctx = nullptr;

    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;
    TitusRHI::BufferHandle m_matricesUbo;
    TitusRHI::BufferHandle m_lightUbo;
};
