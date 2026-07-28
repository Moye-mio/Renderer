#pragma once
// ============================================================================
// 001_Reflective_shadow_map - RSMBufferPass
//
// 后端无关版本：派生自 TitusRHI::IRenderPass。从光源视角渲染 Sponza，
// 输出 3 张 RT（Flux / Normal / Position），用于 ShadingWithRSMPass 的
// 间接光照采样。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;

class RSMBufferPass : public TitusRHI::IRenderPass
{
public:
    RSMBufferPass();
    ~RSMBufferPass() override = default;

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

    // RSM 颜色附件 + 深度（旧实现未挂深度，本期补齐使深度测试有意义）
    TitusRHI::TextureHandle m_fluxTex;
    TitusRHI::TextureHandle m_normalTex;
    TitusRHI::TextureHandle m_positionTex;
    TitusRHI::TextureHandle m_depthTex;
    TitusRHI::RenderTargetHandle m_renderTarget;

    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;

    // 任务 7：与 RSMBuffer_VS.glsl 的 std140 UBO u_Matrices4ProjectionWorld 对齐
    TitusRHI::BufferHandle m_matricesUbo;

    int m_resolution = 256;
    TitusMath::Mat4 m_lightVP{1.0f};
    TitusMath::Vec3 m_lightDir{-1.0f, -0.7071f, 0.0f};
};
