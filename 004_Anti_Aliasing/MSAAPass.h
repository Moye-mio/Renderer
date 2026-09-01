#pragma once
// ============================================================================
// 004_Anti_Aliasing - MSAAPass
//
// 硬件 MSAA：Sponza 画到离屏 multisample RT，ResolveTexture 下采样到
// 1-sample 颜色，再全屏拷回 backbuffer。
// MSAA 走前向路径（与 Deferred 不兼容：per-sample 着色代价过高）。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class MSAAPass : public TitusRHI::IRenderPass
{
public:
    MSAAPass();
    ~MSAAPass() override = default;

    void SetSponza(Sponza* sponza) { m_sponza = sponza; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    void EnsureTargets(TitusRHI::IGDevice& device, uint32_t width, uint32_t height, uint32_t samples);
    void DestroyTargets(TitusRHI::IGDevice& device);
    void CreateScenePipeline(TitusRHI::IGDevice& device, uint32_t samples);

    Sponza* m_sponza = nullptr;
    TechniqueContext* m_ctx = nullptr;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_samples = 0;

    TitusRHI::ShaderHandle m_sceneVS;
    TitusRHI::ShaderHandle m_sceneFS;
    TitusRHI::PipelineHandle m_scenePipeline;

    TitusRHI::TextureHandle m_msaaColor;
    TitusRHI::TextureHandle m_msaaDepth;
    TitusRHI::RenderTargetHandle m_msaaRT;
    TitusRHI::TextureHandle m_resolveColor;
    TitusRHI::SamplerHandle m_blitSampler;

    TitusRHI::ShaderHandle m_blitVS;
    TitusRHI::ShaderHandle m_blitFS;
    TitusRHI::PipelineHandle m_blitPipeline;

    TitusRHI::BufferHandle m_matricesUbo;
    TitusRHI::BufferHandle m_shadingUbo;
};
