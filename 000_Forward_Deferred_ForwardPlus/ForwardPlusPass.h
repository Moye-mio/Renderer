#pragma once
// ============================================================================
// 000_Forward_Deferred_ForwardPlus - ForwardPlusPass
//
// Forward+（Tiled Forward）：同一 Record 内三阶段
//   1) Depth  —— 把视空间 Z 写入 R32F（供 Compute 采样；不读 depth 附件）
//   2) Cull   —— 16×16 tile Compute：视锥 AABB + 深度范围剔点光，写 SSBO
//   3) Shade  —— 几何片元只遍历本 tile 灯表，BRDF 与 Forward / Deferred 对齐
// mode != ForwardPlus 时 Record 早退。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class ForwardPlusPass : public TitusRHI::IRenderPass
{
public:
    ForwardPlusPass();
    ~ForwardPlusPass() override = default;

    void SetSponza(Sponza* sponza) { m_sponza = sponza; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    void RecordDepth(TitusRHI::IGDevice& device, TitusRHI::RenderCommandList& cmd);
    void RecordCull(TitusRHI::IGDevice& device, TitusRHI::RenderCommandList& cmd);
    void RecordShade(TitusRHI::IGDevice& device, TitusRHI::RenderCommandList& cmd);

    Sponza* m_sponza = nullptr;
    TechniqueContext* m_ctx = nullptr;

    uint32_t m_width = 1920;
    uint32_t m_height = 1152;
    uint32_t m_tilesX = 1;
    uint32_t m_tilesY = 1;

    // Depth 预通道：R32F 视空间 Z + D32
    TitusRHI::TextureHandle m_depthVSTex;
    TitusRHI::TextureHandle m_depthTex;
    TitusRHI::RenderTargetHandle m_depthRT;
    TitusRHI::ShaderHandle m_depthVS;
    TitusRHI::ShaderHandle m_depthFS;
    TitusRHI::PipelineHandle m_depthPipeline;

    // Cull Compute
    TitusRHI::ShaderHandle m_cullCS;
    TitusRHI::PipelineHandle m_cullPipeline;
    TitusRHI::SamplerHandle m_depthSampler;

    // Shade
    TitusRHI::ShaderHandle m_shadeVS;
    TitusRHI::ShaderHandle m_shadeFS;
    TitusRHI::PipelineHandle m_shadePipeline;

    TitusRHI::BufferHandle m_matricesUbo;
    TitusRHI::BufferHandle m_lightUbo;
    TitusRHI::BufferHandle m_cullParamsUbo;
    TitusRHI::BufferHandle m_tileLightSSBO;
};
