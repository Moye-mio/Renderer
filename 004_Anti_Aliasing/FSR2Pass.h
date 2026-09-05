#pragma once
// ============================================================================
// 004_Anti_Aliasing - FSR2Pass
//
// FSR 2.0：Sponza 以低于显示分辨率的比例、Halton jitter 前向画到离屏 HDR
// （颜色 + velocity + 视空间深度），再在显示分辨率上做 Lanczos 重建 + 时域累加，
// 最后复用 FSR 1.0 的 RCAS shader 做受限锐化写回 backbuffer。
// 不改 FSRPass / FSR 1.0 shader，只读加载 FSR_RCAS_FS.glsl。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class FSR2Pass : public TitusRHI::IRenderPass
{
public:
    FSR2Pass();
    ~FSR2Pass() override = default;

    void SetSponza(Sponza* sponza) { m_sponza = sponza; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    void EnsureTargets(TitusRHI::IGDevice& device,
                       uint32_t renderWidth, uint32_t renderHeight,
                       uint32_t displayWidth, uint32_t displayHeight);
    void DestroyTargets(TitusRHI::IGDevice& device);
    void ResetHistory();

    Sponza* m_sponza = nullptr;
    TechniqueContext* m_ctx = nullptr;

    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;
    uint32_t m_displayWidth = 0;
    uint32_t m_displayHeight = 0;
    uint32_t m_jitterIndex = 0;
    uint32_t m_historyWrite = 0;
    bool m_historyValid = false;

    TitusMath::Mat4 m_prevViewProj{1.0f};
    bool m_hasPrevViewProj = false;

    TitusRHI::ShaderHandle m_sceneVS;
    TitusRHI::ShaderHandle m_sceneFS;
    TitusRHI::PipelineHandle m_scenePipeline;

    TitusRHI::ShaderHandle m_accumVS;
    TitusRHI::ShaderHandle m_accumFS;
    TitusRHI::PipelineHandle m_accumPipeline;

    TitusRHI::ShaderHandle m_rcasVS;
    TitusRHI::ShaderHandle m_rcasFS;
    TitusRHI::PipelineHandle m_rcasPipeline;

    TitusRHI::TextureHandle m_currColor;
    TitusRHI::TextureHandle m_velocity;
    TitusRHI::TextureHandle m_depthVS;
    TitusRHI::TextureHandle m_depth;
    TitusRHI::RenderTargetHandle m_sceneRT;

    TitusRHI::TextureHandle m_history[2];
    TitusRHI::RenderTargetHandle m_historyRT[2];

    TitusRHI::SamplerHandle m_pointSampler;
    TitusRHI::SamplerHandle m_linearSampler;

    TitusRHI::BufferHandle m_sceneUbo;
    TitusRHI::BufferHandle m_shadingUbo;
    TitusRHI::BufferHandle m_accumUbo;
    TitusRHI::BufferHandle m_rcasUbo;
};
