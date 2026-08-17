#pragma once
// ============================================================================
// 002_Order_Independent_Transparency - WeightedBlendedOITPass
//
// Weighted Blended OIT：不透明 Cornell 写 SceneColor+Depth，再对半透明龙
// 做 Accum / Revealage 两趟，最后全屏 Blend 到 backbuffer。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Scene;
struct TechniqueContext;

class WeightedBlendedOITPass : public TitusRHI::IRenderPass
{
public:
    WeightedBlendedOITPass();
    ~WeightedBlendedOITPass() override = default;

    void SetScene(Scene* scene) { m_scene = scene; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    bool CreateTargets(TitusRHI::IGDevice& device);
    bool CreateShaders(TitusRHI::IGDevice& device);
    bool CreatePipelines(TitusRHI::IGDevice& device);
    void DestroyTargets(TitusRHI::IGDevice& device);

    void SetFullscreenViewport(TitusRHI::RenderCommandList& cmd) const;
    void BindShading(TitusRHI::RenderCommandList& cmd) const;
    void DrawDragons(TitusRHI::RenderCommandList& cmd) const;

    Scene* m_scene = nullptr;
    TechniqueContext* m_ctx = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    TitusRHI::TextureHandle m_sceneColor;
    TitusRHI::TextureHandle m_depth;
    TitusRHI::TextureHandle m_accum;
    TitusRHI::TextureHandle m_revealage;
    TitusRHI::RenderTargetHandle m_opaqueRT;
    TitusRHI::RenderTargetHandle m_accumRT;
    TitusRHI::RenderTargetHandle m_revealRT;

    TitusRHI::ShaderHandle m_sceneVS;
    TitusRHI::ShaderHandle m_sceneFS;
    TitusRHI::ShaderHandle m_accumFS;
    TitusRHI::ShaderHandle m_revealFS;
    TitusRHI::ShaderHandle m_blendVS;
    TitusRHI::ShaderHandle m_blendFS;

    TitusRHI::PipelineHandle m_opaquePipeline;
    TitusRHI::PipelineHandle m_accumPipeline;
    TitusRHI::PipelineHandle m_revealPipeline;
    TitusRHI::PipelineHandle m_blendPipeline;

    TitusRHI::SamplerHandle m_sampler;
    TitusRHI::BufferHandle m_shadingUbo;
};
