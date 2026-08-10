#pragma once
// ============================================================================
// RendererInterface - TitusGfxOverlay
// 默认的 ImGui Overlay UI 管理类：
//   - 默认绘制一个"Renderer Info"面板，展示：
//       · FPS / 平均帧时
//       · 当前 Backend（OpenGL / Vulkan）
//       · GPU 名称
//       · 窗口分辨率
//   - 业务侧可通过 AddPanel(name, lambda) 追加自定义面板，原有默认面板仍保留。
//   - 与 TitusRHI::IMGUI 的关系：
//       · 默认（业务侧未调用 IMGUI::SetUserCallback）：自动启用 OVERLAY，
//         由 IMGUI 模块每帧调用 OVERLAY::Render；
//       · 若业务侧 IMGUI::SetUserCallback 注入了自己的回调，则 OVERLAY 默认
//         面板不会显示；业务侧可以在自己的回调里手动 `OVERLAY::Render()`
//         以保留默认信息面板。
// ============================================================================
#include <functional>
#include <string>

// 业务侧编写自定义面板时要直接调用 ImGui:: API。
#include "imgui.h"

namespace TitusRHI
{
    namespace OVERLAY
    {
        // 业务侧自定义面板回调：在 ImGui::Begin/End 之间被调用一次。
        // 由 OVERLAY 内部负责开/关窗口（窗口标题=panelName）。
        using PanelCallback = std::function<void()>;

        // 启用/禁用所有 OVERLAY 默认面板（不影响 AddPanel 注册的自定义面板）。
        // 默认 true（启用）。
        void SetDefaultPanelEnabled(bool enabled);
        bool IsDefaultPanelEnabled();

        // 注册一个自定义面板。同一 name 重复注册会覆盖。
        // 取消可用 RemovePanel(name)。
        void AddPanel(const std::string& name, PanelCallback cb);
        void RemovePanel(const std::string& name);
        void ClearPanels();

        // 控制默认信息面板的可见性（窗口右上角的关闭按钮等）；可在运行时切换。
        void SetDefaultPanelVisible(bool visible);
        bool IsDefaultPanelVisible();

        // —— 内部接口 ——
        // 由 TitusRHI::IMGUI 模块在 NewFrame -> userCb -> Render 链中调用，
        // 业务代码不应直接调用（除非用户自定义了 IMGUI::SetUserCallback 之后
        // 仍想保留默认信息面板）。
        void Render();
    }
}
