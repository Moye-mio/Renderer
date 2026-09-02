#pragma once
// ============================================================================
// 004_Anti_Aliasing - FXAAPass
//
// 后处理 FXAA：Sponza 前向画到离屏 LDR，再全屏做 luma 边缘检测 + 沿边混合，
// 直接写回 backbuffer。无历史帧、无 RHI 改动。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class FXAAPass : public TitusRHI::IRenderPass
{
public:
    FXAAPass();
    ~FXAAPass() override = default;

    void SetSponza(Sponza* sponza) { m_sponza = sponza; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    void EnsureTargets(TitusRHI::IGDevice& device, uint32_t width, uint32_t height);
    void DestroyTargets(TitusRHI::IGDevice& device);

    Sponza* m_sponza = nullptr;
    TechniqueContext* m_ctx = nullptr;

    uint32_t m_width = 0;
    uint32_t m_height = 0;

    TitusRHI::ShaderHandle m_sceneVS;
    TitusRHI::ShaderHandle m_sceneFS;
    TitusRHI::PipelineHandle m_scenePipeline;

    TitusRHI::ShaderHandle m_fxaaVS;
    TitusRHI::ShaderHandle m_fxaaFS;
    TitusRHI::PipelineHandle m_fxaaPipeline;

    TitusRHI::TextureHandle m_sceneColor;
    TitusRHI::TextureHandle m_sceneDepth;
    TitusRHI::RenderTargetHandle m_sceneRT;
    TitusRHI::SamplerHandle m_linearSampler;

    TitusRHI::BufferHandle m_matricesUbo;
    TitusRHI::BufferHandle m_shadingUbo;
    TitusRHI::BufferHandle m_fxaaUbo;
};
