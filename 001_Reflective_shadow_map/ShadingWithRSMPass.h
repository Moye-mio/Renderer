#pragma once
// ============================================================================
// 001_Reflective_shadow_map - ShadingWithRSMPass
//
// 真正的 Compute Pass。
//   - 创建一张 RGBA32F storage texture 作为输出（u_OutputImage @ binding=0）
//   - 创建 VPL sample coords UBO（binding=1）
//   - 创建 ComputePipeline（载入 ShadingWithRSM_CS.glsl 编译为 compute shader）
//   - Record：BindPipeline → 绑各张输入纹理（GBuffer 3 + RSM 3） → 绑 image →
//     PushConstants(各标量) → Dispatch → PipelineBarrier(Storage→ShaderRead)
// ============================================================================
#include <vector>

#include "RendererInterface/TitusGfxPass.h"

class ShadingWithRSMPass : public TitusRHI::IRenderPass
{
public:
    ShadingWithRSMPass();
    ~ShadingWithRSMPass() override = default;

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    // 输出 storage texture：作为 ShadingTexture 共享给 ScreenQuadPass
    TitusRHI::TextureHandle m_shadingTexture;
    TitusRHI::SamplerHandle m_inputSampler; // 给 6 个 sampler2D 共用

    // Compute Pipeline 与 shader
    TitusRHI::ShaderHandle m_cs;
    TitusRHI::PipelineHandle m_computePipeline;

    // VPL UBO（binding=1）
    TitusRHI::BufferHandle m_vplUbo;

    // Compute Pass 元数据
    int m_cntVPL = 32;
    float m_maxSampleRadius = 25.0f;
    TitusMath::Mat4 m_lightVP{1.0f};
    TitusMath::Vec4 m_lightDirHomo{0.0f};
    uint32_t m_groupCountX = 0;
    uint32_t m_groupCountY = 0;
    int m_rsmResolution = 256;
    uint32_t m_outputWidth = 0;
    uint32_t m_outputHeight = 0;
};
