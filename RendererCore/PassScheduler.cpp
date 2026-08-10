// ============================================================================
// RendererCore - PassScheduler.cpp
// 严格只依赖 IGDevice / RenderCommandList / IRenderPass，无任何后端 API 调用。
// ============================================================================
#include "PassScheduler.h"
#include "IGDevice.h"
#include "RenderCommandList.h"
#include "IRenderPass.h"

#include "TracySupport.h"

#include <algorithm>

namespace TitusRHI
{
    namespace
    {
        // 固定字面量，便于 Tracy 按 zone 名聚合（避免两个 Pass 挤在同一 "Pass::Record"）。
        const char* PassZoneName(ERenderPassEvent e)
        {
            switch (e)
            {
            case ERenderPassEvent::ShadowMap:     return "Pass:ShadowMap";
            case ERenderPassEvent::GBuffer:       return "Pass:GBuffer";
            case ERenderPassEvent::Lighting:      return "Pass:Lighting";
            case ERenderPassEvent::OpaqueShading: return "Pass:OpaqueShading";
            case ERenderPassEvent::Transparent:   return "Pass:Transparent";
            case ERenderPassEvent::PostProcess:   return "Pass:PostProcess";
            case ERenderPassEvent::FinalBlit:     return "Pass:FinalBlit";
            default:                              return "Pass:Other";
            }
        }

        const char* UpdateZoneName(ERenderPassEvent e)
        {
            switch (e)
            {
            case ERenderPassEvent::ShadowMap:     return "Update:ShadowMap";
            case ERenderPassEvent::GBuffer:       return "Update:GBuffer";
            case ERenderPassEvent::Lighting:      return "Update:Lighting";
            case ERenderPassEvent::OpaqueShading: return "Update:OpaqueShading";
            case ERenderPassEvent::Transparent:   return "Update:Transparent";
            case ERenderPassEvent::PostProcess:   return "Update:PostProcess";
            case ERenderPassEvent::FinalBlit:     return "Update:FinalBlit";
            default:                              return "Update:Other";
            }
        }

        const char* RecordZoneName(ERenderPassEvent e)
        {
            switch (e)
            {
            case ERenderPassEvent::ShadowMap:     return "Record:ShadowMap";
            case ERenderPassEvent::GBuffer:       return "Record:GBuffer";
            case ERenderPassEvent::Lighting:      return "Record:Lighting";
            case ERenderPassEvent::OpaqueShading: return "Record:OpaqueShading";
            case ERenderPassEvent::Transparent:   return "Record:Transparent";
            case ERenderPassEvent::PostProcess:   return "Record:PostProcess";
            case ERenderPassEvent::FinalBlit:     return "Record:FinalBlit";
            default:                              return "Record:Other";
            }
        }
    }
    void PassScheduler::AddPass(const std::shared_ptr<IRenderPass>& pass)
    {
        if (!pass) return;
        m_passes.push_back(pass);
        SortPasses();
    }

    void PassScheduler::RemoveAllPasses()
    {
        m_passes.clear();
    }

    void PassScheduler::SortPasses()
    {
        std::stable_sort(m_passes.begin(), m_passes.end(),
            [](const std::shared_ptr<IRenderPass>& a, const std::shared_ptr<IRenderPass>& b) {
                return static_cast<int>(a->passEvent) < static_cast<int>(b->passEvent);
            });
    }

    void PassScheduler::InitAllPasses()
    {
        if (!m_device) return;
        for (auto& p : m_passes) p->Init(*m_device);
    }

    void PassScheduler::DestroyAllPasses()
    {
        if (!m_device) return;
        for (auto& p : m_passes) p->Destroy(*m_device);
        m_passes.clear();
    }

    void PassScheduler::DrawFrame()
    {
        ZoneScopedN("PassScheduler::DrawFrame");
        if (!m_device) return;

        // 1) BeginFrame：后端内部完成 Acquire / 等待 Fence 等同步动作
        {
            ZoneScopedN("BeginFrame");
            m_device->BeginFrame();
        }

        // 2) 取得本帧 CommandList
        RenderCommandList* cmd = nullptr;
        {
            ZoneScopedN("AcquireCommandList");
            cmd = m_device->AcquireCommandList();
        }
        if (!cmd)
        {
            // 后端拒绝出帧（如 swapchain out of date）：仅推进帧计数后返回
            ++m_frameCounter;
            return;
        }

        const uint32_t frameIndex = m_device->GetCurrentFrameIndex();
        const uint32_t imageIndex = frameIndex; // 大多数后端取一致；后端内部已自管 swap image

        // 3) Update + Record
        {
            ZoneScopedN("PassLoop");
            for (auto& p : m_passes)
            {
                const ERenderPassEvent ev = p->passEvent;
                ZoneTransientN(passZone, PassZoneName(ev), true);
                ZoneValue(static_cast<uint64_t>(ev));
                {
                    ZoneTransientN(updateZone, UpdateZoneName(ev), true);
                    p->Update(*m_device, frameIndex);
                }
                {
                    ZoneTransientN(recordZone, RecordZoneName(ev), true);
                    p->Record(*m_device, *cmd, frameIndex, imageIndex);
                }
            }
        }

        // 4) Submit + Present
                // 修复：必须在 Submit *之后* 才调用 RenderImGuiOverlay。
        //   - GL：业务 Pass 仅"录制" lambda 到 cmd，Submit 触发 Replay 才真正
        //     向 GL driver 提交命令；imgui_impl_opengl3 是 immediate 模式，
        //     一调用就立即向 driver 提交。若放在 Submit 之前，imgui 会先于
        //     业务画面打到 default FB，紧接着 Submit 内的 glClear/场景绘制
        //     又把它覆盖掉，最终 SwapBuffers 出去的画面只剩场景没有 GUI。
        //   - VK：imgui draw 必须录到 primaryCmd 中，但 SubmitImpl 内部会
        //     调 primaryCmd->End() 关闭 cmdbuf，所以 VK 端把 imgui 录制
        //     提前挪到了 SubmitImpl 内、End() 之前；这里 Submit 后再调
        //     RenderImGuiOverlay 时 VK 实现已变为 no-op，不会重复执行。
        {
            ZoneScopedN("Submit");
            m_device->Submit(cmd);
        }

        {
            ZoneScopedN("RenderImGuiOverlay");
            m_device->RenderImGuiOverlay();
        }

        {
            ZoneScopedN("Present");
            m_device->Present();
        }

        ++m_frameCounter;
    }
}
