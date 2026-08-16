# 待办计划：统一窗口所有权（Platform 单源）

## 背景

当前窗口管理存在两套并行路径，门面层被迫到处 `if GL else VK`：

| 职责 | GL（现状） | VK（现状） |
|---|---|---|
| 谁建窗 | `APP::InitApp` → `Platform::GLFWWindow` | `VKDevice::OnInitBackend` → `VkWindow` |
| `g.window` | 有值 | 永远 `nullptr` |
| PollEvents | `g.window->PollEvents()` | 门面直接 `glfwPollEvents()` |
| ShouldClose | 问 `g.window` | 问 `device->IsWindowClosed()` |
| 拿 `GLFWwindow*` | `g.window->GetNativeHandle()` | `device->GetWindowNativeHandle()` |
| Present | `IWindow::SwapBuffers` | `device.Present`（swapchain） |
| 尺寸配置 | `WindowDesc` 直接建窗 | 经 `GDeviceDesc` → `WINDOW_KEYWORD` → `VkWindow` |

根因不是「Vulkan 必须自管窗」，而是：

1. 统一 `IWindow` 迁移未完成；VK 曾因把 `GLFWwindow*` 误当成 `VkWindow*` 强转崩溃，用「设备自管」绕开后未收敛。
2. 窗口（平台）与 Surface/Swapchain（设备）职责混在 `VkWindow` / `VKDevice` 中。
3. `Platform` 名义上是统一窗口层，实际几乎只服务 GL。

相关现状代码：

- 门面分支：`RendererInterface/TitusGfx.cpp`（`InitApp` / `UpdateApp` / `ShouldClose` / `INPUT_MANAGER`）
- Platform：`Platform/GLFWWindow.{h,cpp}`（已支持 `GLFW_NO_API`）
- VK 自管窗：`RendererVK/VkWindow.{h,cpp}`、`VKDevice::OnInitBackend`
- 设备旁路接口：`IGDevice::IsWindowClosed` / `GetWindowNativeHandle`

## 目标架构

**唯一原则：窗口只有一个所有者 —— `APP` 持有的 `IWindow`（实现为 `Platform::GLFWWindow`）。**

```
APP::InitApp
  ├─ 始终创建 Platform::GLFWWindow（按 backend 选 OpenGL / NO_API）
  ├─ GDeviceFactory::Create(...)
  └─ device->Init(desc, g.window.get())
        ├─ GL：复用已有 context（glew / clip_control / sRGB）
        └─ VK：从 IWindow::GetNativeHandle() 取 GLFWwindow*
               → glfwCreateWindowSurface → 建 Context / Swapchain
               （不再 new VkWindow，不再 Ignore IWindow*）
```

职责切分：

| 层 | 负责 | 不负责 |
|---|---|---|
| `Platform::GLFWWindow` | glfwInit / 建窗 / PollEvents / ShouldClose / resize 标记 / SwapBuffers(GL) / 暴露 `GLFWwindow*` | Surface、Swapchain、Device |
| `VKDevice` / `VkContext` | 用 `GLFWwindow*` 建 `VkSurfaceKHR`、逻辑设备、Swapchain、同步 | 创建/销毁 GLFW 窗口 |
| `APP` 门面 | 始终持有 `g.window`；输入 / ImGui / ShouldClose 只问窗口 | 后端特例分支 |

目标态行为约束：

- GL / VK / Null：`g.window` 语义一致（Null 可不建窗；VK 必须有窗）。
- `UpdateApp`：只走 `g.window->PollEvents()` / `g.window->ShouldClose()`，删除直接 `glfwPollEvents()` 分支。
- `INPUT_MANAGER` / ImGui：只从 `g.window->GetNativeHandle()` 取 `GLFWwindow*`。
- `IGDevice::IsWindowClosed` / `GetWindowNativeHandle`：标记废弃后删除（或降为 debug-only 断言失败）。
- `TitusVkGraphics::WINDOW_KEYWORD` 中与建窗相关的全局宽高标题：逐步淘汰，改由 `WindowDesc` / `IWindow` 查询。

## 非目标

- 不引入第二套窗口库（SDL 等）。
- 不改变 PassScheduler 帧循环契约。
- 不强制恢复 VK 默认 `Threaded`（线程模式独立议题）。
- 不在本计划内重做 ImGui 绑定，只去掉「取窗」双路径。

## 改造阶段

### Phase 0 — 契约冻结与护栏

- [ ] 在 `Docs/Architecture/99_Pitfalls.md` 增加条目：禁止再新增「设备自管窗口」特例；新代码不得依赖 `VkWindow` 生命周期。
- [ ] 明确 `IWindow::GetNativeHandle()` 语义：**永远是平台原生窗指针**（本工程即 `GLFWwindow*`），禁止再解释为后端包装类型。
- [ ] （可选）加 debug 断言：`InitApp` 后若 backend==Vulkan 则 `g.window != nullptr`。

### Phase 1 — VK 接受外部 `IWindow*`（最小可运行切片）

目标：VK 仍可暂时保留 `VkWindow` 适配层，但**不再自建 GLFW 窗口**。

- [ ] `APP::InitApp`：VK 与 GL 一样创建 `Platform::GLFWWindow`（`backend=Vulkan` → `GLFW_NO_API`）。
- [ ] `VKDevice::OnInitBackend`：使用传入的 `IWindow*`；`GetNativeHandle()` → `GLFWwindow*`。
- [ ] Surface 创建：抽到不依赖 `VkWindow` 的路径，例如：
  - `VkContext::Init(GLFWwindow*)` 或
  - `VkContext::Init(IWindow&)` + 内部 `glfwCreateWindowSurface`
- [ ] Swapchain / resize：改为从 `IWindow` 读 framebuffer 尺寸与 `IsResized`，或设备侧只缓存 `GLFWwindow*` + 查询 GLFW API。
- [ ] 删除 `VKDevice` 内 `m_internalWindow` 自建逻辑；`g.window` 在 VK 下非空。
- [ ] 门面 `UpdateApp` / `ShouldClose`：优先统一到 `g.window`（可暂时保留 device fallback，标 deprecated）。
- [ ] 验证：`--backend=vk` 下 `Test_000_UnifiedTriangle`、`001_Reflective_shadow_map` 能启动、关窗、resize、输入、ImGui 正常。

验收：

- 进程内只有一个 `glfwCreateWindow`。
- 不再出现 `g.window == nullptr` 的 VK 特例主路径。

### Phase 2 — 删除 `VkWindow` / 清理旁路接口

- [ ] 将 `VkSwapchainWrapper` / `VkContext` 的 `VkWindow&` 参数改为 `GLFWwindow*` 或 `IWindow*`。
- [ ] 删除 `RendererVK/VkWindow.{h,cpp}` 及工程引用。
- [ ] 删除或清空 `VKDevice` 对 `WINDOW_KEYWORD` 建窗字段的回写；尺寸只来自 `IWindow` / `WindowDesc`。
- [ ] 从 `IGDevice` 移除（或 no-op + 日志）`IsWindowClosed` / `GetWindowNativeHandle`。
- [ ] `INPUT_MANAGER` / `TitusGfxImGui`：删除 device 取窗分支。
- [ ] `GDeviceDesc.windowWidth/Height`：评估是否仅作「Init 前尚无 window 时的提示」或直接删除，改只信 `IWindow`。
- [ ] 更新架构文档：`Docs/Architecture/00_Overview.md`、`20_RendererVK.md`、`40_Interface.md`、`90_Flows.md`。

验收：

- 全库 grep：`VkWindow`、`m_internalWindow`、`IsWindowClosed`、门面里裸 `glfwPollEvents` 无业务引用。
- GL / VK 示例矩阵冒烟通过。

### Phase 3 — Platform 能力补齐（按需）

- [ ] 若 VK resize / cursor disable 仍依赖旧 `WINDOW_KEYWORD`，把对应能力下沉到 `IWindow` / `WindowDesc`（或 `APP::WINDOW_KEYWORD` 只写 `Platform`）。
- [ ] 统一高 DPI：VK 路径也用 framebuffer 像素尺寸驱动 swapchain extent（与 GL 已有逻辑对齐）。
- [ ] （可选）`Null` 后端明确「无窗」契约，避免误用 `GetNativeHandle`。

## 建议落地顺序（最小风险）

1. **先让 `InitApp` 为 VK 建 `GLFWWindow`，但 VKDevice 暂双轨**：有外部 window 则用外部，否则 fallback 自建（便于 bisect）。
2. 打通 Surface + Swapchain + resize + ShouldClose 后，关掉 fallback。
3. 再删 `VkWindow` 与 `IGDevice` 旁路 API。
4. 最后清文档与全局 `WINDOW_KEYWORD`。

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| `glfwInit` 被 Platform 与旧路径各调一次 | Phase 1 起保证单一建窗点；禁止 Device 再 `glfwInit` |
| ImGui GLFW 回调绑错窗 | 取窗统一后只绑 `g.window` 的 native handle |
| resize 标志丢失 | 确认 `GLFWWindow` 的 framebuffer callback 覆盖 VK 原 `VkWindow` 行为 |
| Surface 扩展 / validation | 保持 `glfwGetRequiredInstanceExtensions` 路径不变，仅换窗来源 |
| 输入路由错乱（历史双窗问题） | 验收项强制「仅一个 GLFWwindow」 |

## 测试清单

- [ ] `Test_000_UnifiedTriangle --backend=gl` / `--backend=vk`
- [ ] `Test_001_VkTriangle`（若仍保留）
- [ ] `000_Forward_Deferred_ForwardPlus`、`001_Reflective_shadow_map` 双后端
- [ ] 关窗、`ShouldClose`、窗口 resize、FlyCamera / `INPUT_MANAGER`
- [ ] ImGui 开关（`enableGUI` true/false）
- [ ] 高 DPI 显示器下 viewport / swapchain extent 一致

## 相关文件（改造触点）

- `RendererInterface/TitusGfx.cpp` / `TitusGfxImGui.cpp`
- `Platform/GLFWWindow.{h,cpp}`
- `RendererCore/IWindow.h`、`GDescs.h`（`GDeviceDesc` 窗口字段）、`IGDevice.h`
- `RendererVK/VKDevice.{h,cpp}`、`VkContext.{h,cpp}`、`VkSwapchainWrapper.{h,cpp}`、`VkWindow.{h,cpp}`
- `RendererVK/Common.h`（`WINDOW_KEYWORD`）
- `Docs/Architecture/{00,20,40,90,99}_*.md`

## 优先级与触发条件

- **优先级：高（架构债）** —— 不影响当前功能正确性，但持续增加门面复杂度与回归成本。
- **触发条件**：下一次涉及窗口 / 输入 / ImGui / VK 启动路径的改动前，优先做完至少 Phase 1；完整删除 `VkWindow` 可放在 Phase 2 独立 PR。
)
