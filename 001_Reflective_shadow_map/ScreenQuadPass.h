#pragma once
// ============================================================================
// 001_Reflective_shadow_map - ScreenQuadPass
//
// 后端无关版本：派生自 TitusRHI::IRenderPass。从共享数据黑板拿到
// "ShadingTexture"（由 ShadingWithRSMPass 写入），用全屏三角形采样到
// 默认 backbuffer 上做最终展示。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class ScreenQuadPass : public TitusRHI::IRenderPass
{
public:
    ScreenQuadPass();
    ~ScreenQuadPass() override = default;

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    TitusRHI::TextureHandle m_shadingTexture; // 引用 ShadingWithRSMPass 输出
    TitusRHI::SamplerHandle m_sampler;
    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;
};
