#pragma once
// ============================================================================
// 003_Toon_Shading - ToonPass
//
// Forward 单 Pass。Cel-Ramp 写到离屏颜色 + crease G-Buffer；背面外扩只写颜色；
// 最后全屏合成内线并拷到 swapchain。ilm / Ramp 不进 TextureSlot。
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
    bool CreateOffscreenTargets(TitusRHI::IGDevice& device);
    bool CreateNprTextures(TitusRHI::IGDevice& device);
    void DestroyOffscreenTargets(TitusRHI::IGDevice& device);
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

    TitusRHI::ShaderHandle m_creaseVs;
    TitusRHI::ShaderHandle m_creaseFs;
    TitusRHI::PipelineHandle m_creasePipeline;
    TitusRHI::BufferHandle m_creaseUbo;
    TitusRHI::SamplerHandle m_creaseSampler;

    uint32_t m_rtWidth = 0;
    uint32_t m_rtHeight = 0;
    TitusRHI::TextureHandle m_sceneColor;
    TitusRHI::TextureHandle m_creaseGBuffer;
    TitusRHI::TextureHandle m_sceneDepth;
    TitusRHI::RenderTargetHandle m_sceneRT;

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
