#pragma once
// ============================================================================
// 0xx_RayQueryHello - RayPipelinePass（RT Pipeline / 路线 B，P1）
// 验证 Ray Tracing Pipeline + SBT + TraceRays 的 raygen→miss/hit 完整流程。
// 仅使用 TitusRHI 后端无关抽象，不接触任何 VkXxx。
//
// 闭环：三角形 BLAS → TLAS → RT 管线（raygen/miss/closesthit + SBT）→
//       TraceRays 写 StorageImage → 全屏三角形采样显示。
// 不支持 RT 管线时（RT 管线句柄无效）优雅回退为仅清屏。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class RayPipelinePass : public TitusRHI::IRenderPass
{
public:
    RayPipelinePass();
    ~RayPipelinePass() override = default;

    void Init   (TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record (TitusRHI::IGDevice&        device,
                 TitusRHI::RenderCommandList& cmd,
                 uint32_t                       frameIndex,
                 uint32_t                       imageIndex) override;

private:
    bool mReady = false;

    uint32_t mWidth  = 1280;
    uint32_t mHeight = 720;

    TitusRHI::BufferHandle                mVertexBuffer;
    TitusRHI::AccelerationStructureHandle mBLAS;
    TitusRHI::AccelerationStructureHandle mTLAS;

    TitusRHI::TextureHandle   mStorageImage;
    TitusRHI::SamplerHandle   mSampler;

    // RT 管线着色器
    TitusRHI::ShaderHandle    mRayGen;
    TitusRHI::ShaderHandle    mMiss;
    TitusRHI::ShaderHandle    mClosestHit;
    TitusRHI::PipelineHandle  mRTPipeline;

    // 显示
    TitusRHI::ShaderHandle    mBlitVS;
    TitusRHI::ShaderHandle    mBlitFS;
    TitusRHI::PipelineHandle  mBlitPipeline;
};
