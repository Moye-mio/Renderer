#pragma once
// ============================================================================
// 002_Order_Independent_Transparency - ScenePass
//
// V1 基线：同一 RenderPass 里先画不透明 Cornell（写深度），再朴素 Alpha
// 混合画半透明 Dragon（测深度、不写深度）。这不是 OIT，只是把场景搭起来。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Scene;
struct TechniqueContext;

class ScenePass : public TitusRHI::IRenderPass
{
public:
    ScenePass();
    ~ScenePass() override = default;

    void SetScene(Scene* scene) { m_scene = scene; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    bool CreateShaders(TitusRHI::IGDevice& device);
    bool CreatePipelines(TitusRHI::IGDevice& device);

    Scene* m_scene = nullptr;
    TechniqueContext* m_ctx = nullptr;

    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_opaquePipeline;
    TitusRHI::PipelineHandle m_transparentPipeline;
    TitusRHI::BufferHandle m_shadingUbo;
};
