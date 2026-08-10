#pragma once
// ============================================================================
// 010_UnifiedTriangle - TrianglePass
// 业务侧 Pass 只继承 TitusRHI::IRenderPass，**仅** include
// "RendererInterface/TitusGfxPass.h"（静态扫描应通过）。
// 同一份 .cpp 不修改即可在 Vulkan / OpenGL 两个后端上运行。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class TrianglePass : public TitusRHI::IRenderPass
{
public:
    TrianglePass();
    ~TrianglePass() override = default;

    // —— TitusRHI::IRenderPass ——
    void Init   (TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Update (TitusRHI::IGDevice& device, uint32_t frameIndex) override;
    void Record (TitusRHI::IGDevice&        device,
                 TitusRHI::RenderCommandList& cmd,
                 uint32_t                       frameIndex,
                 uint32_t                       imageIndex) override;

private:
    TitusRHI::ShaderHandle   mVS;
    TitusRHI::ShaderHandle   mFS;
    TitusRHI::PipelineHandle mPipeline;
};
