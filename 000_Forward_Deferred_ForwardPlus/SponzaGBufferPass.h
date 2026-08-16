#pragma once
// ============================================================================
// 000_Forward_Deferred_ForwardPlus - SponzaGBufferPass
//
// 延迟渲染的几何 Pass：派生自 TitusRHI::IRenderPass，把 Sponza 渲染进 4 张
// RT（Albedo / Normal(view) / Position(view) / Depth），并通过
// RESOURCE_MANAGER::RegisterSharedData 把对应 TextureHandle 共享给后续的
// DeferredLightingPass。所有 GPU 资源经 IGDevice 创建，命令经 RenderCommandList
// 录制，禁止任何原生 glXxx 调用。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class SponzaGBufferPass : public TitusRHI::IRenderPass
{
public:
    SponzaGBufferPass();
    ~SponzaGBufferPass() override = default;

    void SetSponza(Sponza* sponza) { m_sponza = sponza; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    Sponza* m_sponza = nullptr;
    TechniqueContext* m_ctx = nullptr;

    // G-Buffer 颜色 + 深度附件
    TitusRHI::TextureHandle m_albedoTex;
    TitusRHI::TextureHandle m_normalTex;
    TitusRHI::TextureHandle m_positionTex;
    TitusRHI::TextureHandle m_depthTex;
    TitusRHI::RenderTargetHandle m_renderTarget;

    // Shader / Pipeline
    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;

    // 与 Sponza_VS.glsl 的 std140 UBO u_Matrices4ProjectionWorld 对齐：
    //   binding=0：mat4 u_ProjectionMatrix; mat4 u_ViewMatrix;
    TitusRHI::BufferHandle m_matricesUbo;

    uint32_t m_width = 1920;
    uint32_t m_height = 1152;
};
