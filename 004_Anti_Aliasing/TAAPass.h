#pragma once
// ============================================================================
// 004_Anti_Aliasing - TAAPass
//
// Temporal AA：Halton(2,3) 抖动投影画 Sponza（颜色 + RG16F velocity），
// 再按上一帧 view-proj 重投影 history，邻域 clamp 后写入双缓冲 HDR。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class TAAPass : public TitusRHI::IRenderPass
{
public:
    TAAPass();
    ~TAAPass() override = default;

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
    void ResetHistory();

    Sponza* m_sponza = nullptr;
    TechniqueContext* m_ctx = nullptr;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_jitterIndex = 0;
    uint32_t m_historyWrite = 0;
    bool m_historyValid = false;

    TitusMath::Mat4 m_prevViewProj{1.0f};
    bool m_hasPrevViewProj = false;

    TitusRHI::ShaderHandle m_sceneVS;
    TitusRHI::ShaderHandle m_sceneFS;
    TitusRHI::PipelineHandle m_scenePipeline;

    TitusRHI::ShaderHandle m_resolveVS;
    TitusRHI::ShaderHandle m_resolveFS;
    TitusRHI::PipelineHandle m_resolvePipeline;

    TitusRHI::ShaderHandle m_blitVS;
    TitusRHI::ShaderHandle m_blitFS;
    TitusRHI::PipelineHandle m_blitPipeline;

    TitusRHI::TextureHandle m_currColor;
    TitusRHI::TextureHandle m_velocity;
    TitusRHI::TextureHandle m_depth;
    TitusRHI::RenderTargetHandle m_sceneRT;
    TitusRHI::TextureHandle m_history[2];
    TitusRHI::RenderTargetHandle m_historyRT[2];

    TitusRHI::SamplerHandle m_pointSampler;
    TitusRHI::SamplerHandle m_linearSampler;

    TitusRHI::BufferHandle m_sceneUbo;
    TitusRHI::BufferHandle m_shadingUbo;
    TitusRHI::BufferHandle m_resolveUbo;
};
