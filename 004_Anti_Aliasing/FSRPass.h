#pragma once
// ============================================================================
// 004_Anti_Aliasing - FSRPass
//
// FSR 1.0：Sponza 以低于显示分辨率的比例前向画到离屏 LDR，EASU 边缘自适应上采样
// 到显示分辨率，再由 RCAS 做受限锐化写回 backbuffer。纯空间算法，无历史帧。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class FSRPass : public TitusRHI::IRenderPass
{
public:
    FSRPass();
    ~FSRPass() override = default;

    void SetSponza(Sponza* sponza) { m_sponza = sponza; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    // renderWidth/Height 是场景渲染分辨率，displayWidth/Height 是 EASU 输出分辨率。
    void EnsureTargets(TitusRHI::IGDevice& device,
                       uint32_t renderWidth, uint32_t renderHeight,
                       uint32_t displayWidth, uint32_t displayHeight);
    void DestroyTargets(TitusRHI::IGDevice& device);

    Sponza* m_sponza = nullptr;
    TechniqueContext* m_ctx = nullptr;

    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;
    uint32_t m_displayWidth = 0;
    uint32_t m_displayHeight = 0;

    TitusRHI::ShaderHandle m_sceneVS;
    TitusRHI::ShaderHandle m_sceneFS;
    TitusRHI::PipelineHandle m_scenePipeline;

    TitusRHI::ShaderHandle m_easuVS;
    TitusRHI::ShaderHandle m_easuFS;
    TitusRHI::PipelineHandle m_easuPipeline;

    TitusRHI::ShaderHandle m_rcasVS;
    TitusRHI::ShaderHandle m_rcasFS;
    TitusRHI::PipelineHandle m_rcasPipeline;

    TitusRHI::TextureHandle m_sceneColor;
    TitusRHI::TextureHandle m_sceneDepth;
    TitusRHI::RenderTargetHandle m_sceneRT;

    TitusRHI::TextureHandle m_easuColor;
    TitusRHI::RenderTargetHandle m_easuRT;

    TitusRHI::SamplerHandle m_linearSampler;

    TitusRHI::BufferHandle m_matricesUbo;
    TitusRHI::BufferHandle m_shadingUbo;
    TitusRHI::BufferHandle m_easuUbo;
    TitusRHI::BufferHandle m_rcasUbo;
};
