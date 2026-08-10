#pragma once
// ============================================================================
// 001_Reflective_shadow_map - SponzaGBufferPass
//
// 后端无关版本：派生自 TitusRHI::IRenderPass，所有 GPU 资源通过
// IGDevice 创建，所有命令通过 RenderCommandList 录制。仅 include
// "RendererInterface/TitusGfxPass.h"，禁止任何 Renderer/*.h。
//
// 输出：4 张 RT（Albedo / Normal / Position / Depth），并通过
// RESOURCE_MANAGER::RegisterSharedData 把对应 TextureHandle 共享给
// 后续 Pass（RSMBufferPass / ShadingWithRSMPass / ScreenQuadPass）。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;

class SponzaGBufferPass : public TitusRHI::IRenderPass
{
public:
    SponzaGBufferPass();
    ~SponzaGBufferPass() override = default;

    // 业务侧把 Sponza 引用注入；Sponza 持 GpuModelHandle，本类只读
    void SetSponza(Sponza* sponza) { m_sponza = sponza; }

    // —— TitusRHI::IRenderPass ——
    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    Sponza* m_sponza = nullptr;

    // GBuffer 颜色 + 深度附件
    TitusRHI::TextureHandle m_albedoTex;
    TitusRHI::TextureHandle m_normalTex;
    TitusRHI::TextureHandle m_positionTex;
    TitusRHI::TextureHandle m_depthTex;
    TitusRHI::RenderTargetHandle m_renderTarget;

    // Shader / Pipeline
    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;

    // 与 Sponza_VS.glsl 的 std140 UBO u_Matrices4ProjectionWorld 对齐
    //   binding=0：mat4 u_ProjectionMatrix; mat4 u_ViewMatrix;
    TitusRHI::BufferHandle m_matricesUbo;

    uint32_t m_width = 1920;
    uint32_t m_height = 1152;
};
