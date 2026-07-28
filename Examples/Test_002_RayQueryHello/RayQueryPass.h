#pragma once
// ============================================================================
// 0xx_RayQueryHello - RayQueryPass
// 业务侧 Pass，仅 include "RendererInterface/TitusGfxPass.h"，只使用
// TitusRHI 后端无关抽象类型（句柄 / Desc / 枚举），不接触任何 VkXxx。
//
// 闭环（需求 13.2）：
//   构建三角形 BLAS → 构建 TLAS → compute 着色器内 rayQuery 求交 →
//   写入 StorageImage → 全屏三角形采样显示。
// 不支持光追时（GetCaps().supportsRayTracing == false）优雅提示并仅清屏
// （需求 13.3）。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class RayQueryPass : public TitusRHI::IRenderPass
{
public:
    RayQueryPass();
    ~RayQueryPass() override = default;

    void Init   (TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record (TitusRHI::IGDevice&        device,
                 TitusRHI::RenderCommandList& cmd,
                 uint32_t                       frameIndex,
                 uint32_t                       imageIndex) override;

private:
    bool mRayTracingSupported = false;

    uint32_t mWidth  = 1280;
    uint32_t mHeight = 720;
    uint32_t mGroupCountX = 0;
    uint32_t mGroupCountY = 0;

    // 光追资源
    TitusRHI::BufferHandle                mVertexBuffer;
    TitusRHI::AccelerationStructureHandle mBLAS;
    TitusRHI::AccelerationStructureHandle mTLAS;

    // compute（rayQuery）→ storage image
    TitusRHI::TextureHandle   mStorageImage;
    TitusRHI::SamplerHandle   mSampler;
    TitusRHI::ShaderHandle    mComputeShader;
    TitusRHI::PipelineHandle  mComputePipeline;

    // 显示（全屏 blit）
    TitusRHI::ShaderHandle    mBlitVS;
    TitusRHI::ShaderHandle    mBlitFS;
    TitusRHI::PipelineHandle  mBlitPipeline;
};
