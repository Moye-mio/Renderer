#pragma once
// ============================================================================
// 003_Toon_Shading - ToonPass
//
// M1：Forward 单 Pass，采样 Diffuse + 方向光，画到默认 backbuffer。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Scene;
struct TechniqueContext;

class ToonPass : public TitusRHI::IRenderPass
{
public:
    ToonPass();
    ~ToonPass() override = default;

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
    bool CreatePipeline(TitusRHI::IGDevice& device);

    Scene* m_scene = nullptr;
    TechniqueContext* m_ctx = nullptr;

    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;
    TitusRHI::BufferHandle m_shadingUbo;
};
