#pragma once
// ============================================================================
// RendererCore - PassScheduler
// 后端无关的 Pass 调度器：仅依赖 IGDevice 与 RenderCommandList。
// DrawFrame 流程对两个后端完全相同：
//   BeginFrame → AcquireCommandList → 遍历 Pass.Update/Record → Submit → Present
// 设计参考：RendererCore 设计方案 §4、需求 6.2/6.3。
// ============================================================================
#include <vector>
#include <memory>

namespace TitusRHI
{
    class IGDevice;
    class RenderCommandList;
    class IRenderPass;

    class PassScheduler
    {
    public:
        PassScheduler() = default;
        ~PassScheduler() = default;

        PassScheduler(const PassScheduler&)            = delete;
        PassScheduler& operator=(const PassScheduler&) = delete;

        // 设备引用（不拥有）。在 Init 时设置，DrawFrame 期间不可换。
        void SetDevice(IGDevice* device) { m_device = device; }

        // —— Pass 管理 ——
        void AddPass(const std::shared_ptr<IRenderPass>& pass);
        void RemoveAllPasses();

        // 通过 IGDevice 完成所有 Pass 的 Init / Destroy（统一的资源生命周期）
        void InitAllPasses();
        void DestroyAllPasses();

        // 调度一帧渲染：BeginFrame → 录制所有 Pass → Submit → Present
        void DrawFrame();

        size_t GetPassCount() const { return m_passes.size(); }

    private:
        void SortPasses();

        IGDevice*                                m_device = nullptr;
        std::vector<std::shared_ptr<IRenderPass>>  m_passes;
        uint32_t                                   m_frameCounter = 0;
    };
}
