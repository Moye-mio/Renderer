# RendererInterface 详解（门面层 / Facade）

> 第 2/3 遍：逐文件 → 命名空间 API 组 → 职责 + 热点函数下钻（带行号）。
> 命名空间：对外 `TitusRHI`（子命名空间见下），内部实现 `TitusRHIInterface`。
> 定位：渲染层对外**唯一入口**。外部只 include `RendererInterface/*.h`，禁止直接 include `RendererCore/`、`RendererGL/`、`RendererVK/`、`Platform/`。

---

## 0. 门面层如何"隐藏后端"

- 业务侧的 CI 静态扫描（`tools/check_no_backend_headers.bat`）只检查业务源码的 `#include` 字面文本；本层内部通过 `../RendererCore/...` 相对路径"上行转发"底层头，不构成对业务的泄露。
- 只有 `GDeviceFactory.cpp` 一个文件被允许**同时** include `RendererGL/GLDevice.h` 与 `RendererVK/VKDevice.h`，是唯一后端分发点（见 `00_Overview.md §6` 的依赖方向约束）。

---

## 1. 文件全景

| 文件 | 一句话职责 |
|---|---|
| `TitusGfx.h` | 唯一对外门面头：`TitusRHI` 及全部子命名空间声明 + `GpuModelHandle` |
| `TitusGfx.cpp` | 门面实现：APP 生命周期、后端/线程装配、各子命名空间落地 |
| `TitusGfxPass.h` | Pass 实现侧入口：聚合转发 12 个 RendererCore 核心头 |
| `TitusGfxEnums.h` | 枚举转发头（`GBackend`/`GThreadingMode`/`ERenderPassEvent`） |
| `TitusGfxAsset.h` | 资产相关对外类型转发 |
| `TitusGfxImGui.{h,cpp}` | ImGui 门面（初始化 + Overlay 回调注入） |
| `TitusGfxOverlay.{h,cpp}` | Overlay 绘制封装 |
| `GDeviceFactory.{h,cpp}` | 后端分发器（唯一同时引用 GL/VK 的文件） |

---

## 2. 枚举转发 · `TitusGfxEnums.h`

无自定义定义，仅三条转发：`../RendererCore/GEnums.h`（`GBackend`，权威定义 `GEnums.h:15`）、`../RendererCore/GThreadingMode.h`（`GThreadingMode`，`:13`，**GL/VK 均默认 Direct**）、`../RendererCore/IRenderPass.h`（`ERenderPassEvent`）。让业务只 include 门面头即可拿到形参所需枚举。

---

## 3. 门面头 · `TitusGfx.h` ★

### 3.1 顶层类型
| 名称 | 行号 | 职责 |
|---|---|---|
| `class IRenderPass`（前向声明） | `:58` | Pass 基类，完整定义在 RendererCore，由 `TitusGfxPass.h` 转发 |
| `struct GpuModelTag` | `:67` | 模型句柄类型标签（仅 Interface 内部，不下沉 Core） |
| `struct GpuModelHandle` | `:71` | 不透明模型句柄：`uint64_t id` + `IsValid`/`==`/`!=` |

### 3.2 子命名空间 API 组
| 命名空间 | 行号 | 职责 | 关键 API |
|---|---|---|---|
| `APP` | `:90` | 应用生命周期 + 后端/线程/Validation + 截图 | `SetBackend`/`SetThreadingMode`/`SetEnableValidation`/`Get*`/`ParseCommandLine`；`InitApp`/`UpdateApp`/`ShutdownApp`；`ShouldClose`/`RequestClose`/`WaitIdle`；`CaptureScreenshot`/`CaptureScreenshotNextFrameHideUi`/`GetLastScreenshot*`；`AddPass`；`UploadGpuModel`/`DestroyGpuModel`；`RunUnitTests` |
| `WINDOW_KEYWORD` | `:147` | 窗口属性 | `SetWindowSize`(149)/`SetIsCursorDisable`(150)/`GetWindowWidth`(151)/`GetWindowHeight`(152)/`SetWindowTitle`(153) |
| `COMPONENT_CONFIG` | `:159` | 组件开关 | `SetIsEnableGUI`(161) |
| `RESOURCE_MANAGER` | `:173` | Pass 注册 + 共享数据黑板 | `RegisterRenderPass`(178)/`RemoveAllPasses`(179)；模板 `RegisterSharedData<T>`(209)/`GetSharedDataByName<T>`(222) |
| `INPUT_MANAGER` | `:240` | 输入查询 | 键值常量(243-256)；`GetKeyStatus`(259)/`GetMouseButton`(262)/`GetCursorPos`(266)/`SetCursorDisabled`(269)/`IsCursorDisabled`(270) |
| `CAMERA` | `:279` | 主相机访问 + 内置飞行相机 | 只读 `GetMainCamera*`(281-287)；注入 `SetMainCamera*`(282-294)；`struct FlyCameraConfig`(308)；`EnableBuiltinFlyCamera`(324)/`SetBuiltinFlyCameraConfig`(326) |

### 3.3 共享数据黑板（类型安全）
`RESOURCE_MANAGER::detail`：`IsAllowedSharedDataType<T>` 用白名单约束可存类型（`int/float/TitusMath::Vec3/Vec4/Mat4/GpuModelHandle/TextureHandle/BufferHandle/SamplerHandle`）；模板 `RegisterSharedData<T>`/`GetSharedDataByName<T>` 内联并携带 `static_assert`，底层走 `StoreSharedDataAny`/`FindSharedDataAny`。失败时 `GetSharedDataByName` 返回 `T{}`。

> **设计点**：黑板让不同 Pass 间以字符串 key 传递跨帧共享数据（如 GBuffer 纹理句柄），且在**编译期**限制可存类型，避免误存后端原生类型。

---

## 4. Pass 实现侧入口 · `TitusGfxPass.h`

业务侧要继承 `IRenderPass`、调用 `IGDevice`/`RenderCommandList`、操作 Handle/Desc/Enum 时的**唯一 include**。聚合转发 12 个 RendererCore 头：`IRenderPass.h`、`IGDevice.h`、`RenderCommandList.h`、`RayTracingManager.h`、`GHandle.h`、`GEnums.h`、`GDescs.h`、`ShaderAsset.h`、`Material.h`、`ShaderParameterSet.h`、`GpuModel.h`、`GpuMesh.h`。并提供 `namespace TitusRHI` 内联 helper（如 `GetMeshSharedLayout` 等）方便业务复用共享顶点布局。

---

## 5. 后端装配 · `GDeviceFactory.{h,cpp}` ★

`namespace TitusRHIInterface` 的 `GDeviceFactory`（`GDeviceFactory.cpp`）：
- `CreateRealDevice(backend)`（`:29`）：`switch(backend)` →
  - `OpenGL` → `new TitusGraphics::GLDevice()`（需 `RENDERER_ENABLE_GL`，`:35-41`）
  - `Vulkan` → `new TitusVkGraphics::VKDevice()`（需 `RENDERER_ENABLE_VK`，`:45-51`）
  - `Null` → `new TitusRHI::GDeviceHeadless()`（headless/测试，`:57`）
  - 未启用/未知 → 打错误日志返回 `nullptr`
- `Create(backend, threading)`（`:66`）：
  - `threading == Threaded` → 先 `CreateRealDevice`，再外包 `new GDeviceMainThread(std::move(real))`（`:79`）
  - `Direct`/`NonThreaded` → 直接返回真实设备

> 这是 `00_Overview.md §5.7`"工厂桥接注入"的落地：后端选择 + 线程模式包装都收敛在此。

---

## 6. ImGui 门面 · `TitusGfxImGui.{h,cpp}` / `TitusGfxOverlay.{h,cpp}`

- 提供 ImGui 初始化与业务侧的 Overlay 绘制封装；
- 通过 `IGDevice::SetImGuiOverlayCallback(cb, userData)`（见 `10_RendererCore.md §4`）把真正的 imgui 绘制回调注入后端；
- 后端只负责"在正确时机（`PassScheduler::DrawFrame` 中 Submit 后 / VK 的 `SubmitImpl` End 前）以正确的 FB/RenderPass 状态调用回调"，实现"后端无关的 Overlay"（时序坑见 `90_Flows.md`/`99_Pitfalls.md`）。
- 默认「Renderer Info」面板含 **Tracy Capture** 勾选（`TitusTracySetCaptureEnabled`）与连接状态；完整语义见 `50_Tracy.md`。自定义 `IMGUI::SetUserCallback` 时若未调用 `OVERLAY::Render()`，该控件不会出现。
- 同一面板提供 **Screenshot**：`Hide ImGui for capture` + `Capture Screenshot`。不勾选 Hide 时本帧含 Overlay 截图；勾选后下一帧跳过 ImGui 再截。手动截图不退出进程。

---

## 7. APP 装配流程（`TitusGfx.cpp` 要点）

1. `ParseCommandLine`：解析 `--backend=gl|vk|null`、`--threading=direct|threaded|nonthreaded`、`--validation=on|off`，以及截图相关：
   - `--screenshot-at=<seconds>`：墙钟到达后自动截一张（含 Overlay）；未指定则不自动截。
   - `--screenshot-dir=<path>`：输出目录覆盖（默认 `$(SolutionDir)<LoggerAppName>/results/`）。
   - `--quit-after-screenshot=on|off`：自动截图成功后是否退出（默认 `on`）。
2. `InitApp`：
   - 经 `GDeviceFactory::Create(backend, threading)` 得到 `IGDevice`（可能已被 `GDeviceMainThread` 包裹）；
   - 装配窗口（GL 由 Interface 管 `IWindow`；VK 自管窗口，`g.window` 可为 nullptr）；
   - `device->Init(desc, window)`；
   - `PassScheduler::SetDevice` + `SetBeforePresentCallback`（截图读回挂在 Present 前）+ `InitAllPasses`。
3. `UpdateApp`：主循环每帧 `PassScheduler::DrawFrame`；`ShouldClose` 对 VK 自管窗口经 `IGDevice::IsWindowClosed` 问询。CLI/手动截图状态机在 NewFrame 与 DrawFrame 之间编排。
4. `AddPass`：把业务 `IRenderPass` 交给 `PassScheduler`（按 `passEvent` 排序）。
5. `ShutdownApp`：`WaitIdle` → `DestroyAllPasses` → `device->Shutdown`。

### 7.1 截图读回要点

- `IGDevice::ReadbackBackbuffer` → RGBA8（图像顶部为第 0 行）；`AssetLoader::SaveImage2DPNG` 写盘。
- **GL**：`DrawFrame` 内 ImGui 已画到默认 FBO 后、`Present`（空）/外层 `SwapBuffers` 前读回。
- **VK**：swapchain `TRANSFER_SRC`；必须在 `vkQueuePresentKHR` **之前**读回（故挂在 `PassScheduler` 的 BeforePresent 钩子）。
- 文件名：`shot_<gl|vk>_<YYYYMMDD_HHMMSS>.png`。
