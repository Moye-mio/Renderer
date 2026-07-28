#pragma once
// ============================================================================
// Test_001_VkTriangle - TrianglePass
// 方案 A 迁移：业务 Pass 统一继承 TitusRHI::IRenderPass，**仅** include
// "RendererInterface/TitusGfxPass.h"，不再依赖 RendererVK 的 IVkRenderPass /
// VkContext / VkCommandBufferWrapper 等后端专用类型。
// 同一份 .cpp 不修改即可在 Vulkan / OpenGL 两个后端上运行（默认 Vulkan）。
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
    TitusRHI::ShaderHandle   m_vs;
    TitusRHI::ShaderHandle   m_fs;
    TitusRHI::PipelineHandle m_pipeline;
};
