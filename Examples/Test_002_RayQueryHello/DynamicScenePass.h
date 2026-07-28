#pragma once
// ============================================================================
// 0xx_RayQueryHello - DynamicScenePass（P2，任务 16 / 需求 15）
// 动态场景光追示例：用 RayTracingManager 管理多个引用同一
// BLAS 的 instance（BLAS 去重），每帧移动 instance 并 refit TLAS（增量更新），
// 再用 rayQuery compute 渲染。验证需求 15.1/15.2/15.3。
// 仅使用 TitusRHI 后端无关抽象，不接触任何 VkXxx。
// ============================================================================
#include <memory>

#include "RendererInterface/TitusGfxPass.h"

class DynamicScenePass : public TitusRHI::IRenderPass
{
public:
    DynamicScenePass();
    ~DynamicScenePass() override = default;

    void Init   (TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Update (TitusRHI::IGDevice& device, uint32_t frameIndex) override;
    void Record (TitusRHI::IGDevice&        device,
                 TitusRHI::RenderCommandList& cmd,
                 uint32_t                       frameIndex,
                 uint32_t                       imageIndex) override;

private:
    bool mReady = false;
    uint32_t mWidth  = 1280;
    uint32_t mHeight = 720;
    uint32_t mGroupCountX = 0;
    uint32_t mGroupCountY = 0;
    float    mTime = 0.0f;

    using Mgr = TitusRHI::RayTracingManager;
    std::unique_ptr<Mgr> mASManager;
    static constexpr int kInstanceCount = 3;
    Mgr::InstanceID mInstances[kInstanceCount] = {};

    TitusRHI::BufferHandle   mVertexBuffer;

    TitusRHI::TextureHandle  mStorageImage;
    TitusRHI::SamplerHandle  mSampler;
    TitusRHI::ShaderHandle   mComputeShader;
    TitusRHI::PipelineHandle mComputePipeline;

    TitusRHI::ShaderHandle   mBlitVS;
    TitusRHI::ShaderHandle   mBlitFS;
    TitusRHI::PipelineHandle mBlitPipeline;
};
