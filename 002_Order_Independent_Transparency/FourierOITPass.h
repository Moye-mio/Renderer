#pragma once
// ============================================================================
// 002_Order_Independent_Transparency - FourierOITPass
//
// Fourier Opacity OIT：用傅里叶级数拟合沿视线的消光函数，为每个半透明片元
// 解析地重建出它前方的透射率，再套用加权平均框架合成。不排序、不用链表、
// 显存恒定，与场景透明层数无关。
//
// Opaque Cornell → Coefficient（2 张 RGBA32F MRT 加性累加）→
// Reconstruct（重建透射率并累加加权颜色）→ 全屏 Merge。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Scene;
struct TechniqueContext;

class FourierOITPass : public TitusRHI::IRenderPass
{
public:
    FourierOITPass();
    ~FourierOITPass() override = default;

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
    void BindShadingWithCoefficients(TitusRHI::RenderCommandList& cmd) const;
    void DrawDragons(TitusRHI::RenderCommandList& cmd) const;

    Scene* m_scene = nullptr;
    TechniqueContext* m_ctx = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    TitusRHI::TextureHandle m_sceneColor;
    TitusRHI::TextureHandle m_depth;
    TitusRHI::TextureHandle m_coefficientOne; // (片元计数, a0, a1, b1)
    TitusRHI::TextureHandle m_coefficientTwo; // (a2, b2, a3, b3)
    TitusRHI::TextureHandle m_accum;
    TitusRHI::RenderTargetHandle m_opaqueRT;
    TitusRHI::RenderTargetHandle m_coefficientRT;
    TitusRHI::RenderTargetHandle m_accumRT;

    TitusRHI::ShaderHandle m_sceneVS;
    TitusRHI::ShaderHandle m_sceneFS;
    TitusRHI::ShaderHandle m_coefficientFS;
    TitusRHI::ShaderHandle m_reconstructFS;
    TitusRHI::ShaderHandle m_mergeVS;
    TitusRHI::ShaderHandle m_mergeFS;

    TitusRHI::PipelineHandle m_opaquePipeline;
    TitusRHI::PipelineHandle m_coefficientPipeline;
    TitusRHI::PipelineHandle m_reconstructPipeline;
    TitusRHI::PipelineHandle m_mergePipeline;

    TitusRHI::SamplerHandle m_sampler;
    TitusRHI::BufferHandle m_shadingUbo;
};
