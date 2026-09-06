#pragma once
// ============================================================================
// 006_Dynamic_Diffuse_GI - SponzaGBufferPass
// 写出世界空间 Albedo / Normal / Position + Depth，供 DDGI 着色采样。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

#include <vector>

class Sponza;
struct DDGIContext;

class SponzaGBufferPass : public TitusRHI::IRenderPass
{
public:
    SponzaGBufferPass();
    ~SponzaGBufferPass() override = default;

    void SetSponza(Sponza* sponza) { m_sponza = sponza; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    Sponza* m_sponza = nullptr;

    TitusRHI::TextureHandle m_albedoTex;
    TitusRHI::TextureHandle m_normalTex;
    TitusRHI::TextureHandle m_positionTex;
    TitusRHI::TextureHandle m_depthTex;
    TitusRHI::RenderTargetHandle m_renderTarget;

    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;
    // 相机矩阵 UBO 按帧槽位轮转：单份 UBO 会被 CPU 在上一帧还没跑完时改写。
    std::vector<TitusRHI::BufferHandle> m_matricesUbos;

    uint32_t m_width = 1920;
    uint32_t m_height = 1080;
};
