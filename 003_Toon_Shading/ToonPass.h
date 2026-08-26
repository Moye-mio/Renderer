#pragma once
// ============================================================================
// 003_Toon_Shading - ToonPass
//
// Forward 单 Pass。先画 Diffuse / Cel-Ramp，再（可选）背面外扩描边。
// ilm / Ramp 不进 TextureSlot，由本 Pass 自管 TextureHandle。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

#include <string>

class Scene;
struct TechniqueContext;

class ToonPass : public TitusRHI::IRenderPass
{
public:
    ToonPass();
    ~ToonPass() override = default;

    void SetScene(Scene* scene) { m_scene = scene; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }
    void SetTextureDir(const std::string& dir) { m_textureDir = dir; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    bool CreateShaders(TitusRHI::IGDevice& device);
    bool CreatePipeline(TitusRHI::IGDevice& device);
    bool CreateNprTextures(TitusRHI::IGDevice& device);
    void DestroyNprTextures(TitusRHI::IGDevice& device);

    Scene* m_scene = nullptr;
    TechniqueContext* m_ctx = nullptr;
    std::string m_textureDir;

    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;
    TitusRHI::BufferHandle m_shadingUbo;

    TitusRHI::ShaderHandle m_outlineVs;
    TitusRHI::ShaderHandle m_outlineFs;
    TitusRHI::PipelineHandle m_outlinePipeline;
    TitusRHI::BufferHandle m_outlineUbo;

    TitusRHI::TextureHandle m_bodyIlm;
    TitusRHI::TextureHandle m_hairIlm;
    TitusRHI::TextureHandle m_faceIlm;
    TitusRHI::TextureHandle m_bodyRamp;
    TitusRHI::TextureHandle m_hairRamp;
    TitusRHI::SamplerHandle m_ilmSampler;
    TitusRHI::SamplerHandle m_rampSampler;

    // TextureDesc.debugName 指向这些字符串，必须活过 CreateTexture。
    std::string m_bodyIlmName;
    std::string m_hairIlmName;
    std::string m_bodyRampName;
    std::string m_hairRampName;
    std::string m_faceIlmName;
};
