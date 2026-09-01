#pragma once
// ============================================================================
// 004_Anti_Aliasing - ScenePass
//
// 无抗锯齿基线（technique0）：把 Sponza 画到默认 backbuffer，片元做 Lambert + 漫反射。
// mode != None 时 Record 早退。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class ScenePass : public TitusRHI::IRenderPass
{
public:
    ScenePass();
    ~ScenePass() override = default;

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
    TitusRHI::BufferHandle m_shadingUbo;
};
