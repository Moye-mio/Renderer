# RendererInterface（门面层 / Facade）

> 渲染层对外**唯一**入口。外部模块只 include 本目录下的 `TitusGfx.h`，**不**得直接 include `Renderer/`、`RendererCore/`、`RendererVK/` 中的任何头文件。

---

## 设计要点

- 命名空间 `TitusGfx::APP / WINDOW_KEYWORD / COMPONENT_CONFIG / RESOURCE_MANAGER / INPUT_MANAGER / CAMERA` 统一对外
- 后端选择：`--backend=gl|vk|null`（命令行）或 `APP::SetBackend(...)`
- 线程模式：`--threading=direct|threaded|nonthreaded` 或 `APP::SetThreadingMode(...)`
- 默认线程模式：VK→`Threaded`、GL→`Direct`（任务 6 接入 `GDeviceMainThread`（主线程门面）后真正生效）
- 业务側 Pass 继承使用跨后端接口：`#include "RendererInterface/TitusGfxPass.h"` 后继承 `::TitusGfx::IRenderPass`
- 示例工程（`Examples/010_UnifiedTriangle`）**仅** include `RendererInterface/*.h`、**仅**链接 `RendererInterface.lib`；同一份源码以 `--backend=gl` 与 `--backend=vk` 启动均显示三角形（需求 9.3 / 10.2）

## 依赖方向

```
RendererInterface ── 依赖 ──▶ Renderer (GL)
        │                       │
        ├── 依赖 ──▶ RendererCore ◀──依赖── Renderer / RendererVK
        │                       ▲
        ├── 依赖 ──▶ RendererVK (VK)   依赖
        │
        └── 依赖 ──▶ Platform   (IWindow GLFW 实现）
```

只有 `RendererInterface/GDeviceFactory.cpp` **同时**引用 `Renderer/GLDevice.h` 与 `RendererVK/VKDevice.h`，其余源文件不得双向引用。
CI 脚本 `tools\check_deps_direction.bat` 在 `RendererInterface` 项目的 PreBuildEvent 中作为硬约束校验上述方向。

## 旧 API → 新 API 对照表

| 旧 API                                | 新 API                                  |
|---------------------------------------|-----------------------------------------|
| `TitusGraphics::APP::InitApp()`       | `TitusGfx::APP::InitApp()`              |
| `TitusGraphics::APP::UpdateApp()`     | `TitusGfx::APP::UpdateApp()`            |
| （无）                                | `TitusGfx::APP::ShutdownApp()`          |
| （无）                                | `TitusGfx::APP::ShouldClose()`          |
| （无）                                | `TitusGfx::APP::WaitIdle()`             |
| （无）                                | `TitusGfx::APP::AddPass(pass)`          |
| `TitusGraphics::WINDOW_KEYWORD::*`    | `TitusGfx::WINDOW_KEYWORD::*`           |
| `TitusGraphics::CAMERA::*`            | `TitusGfx::CAMERA::*`                   |
| `TitusVkGraphics::*`                  | `TitusGfx::*`（按 `--backend=vk` 启动） |
| `class IRenderPass : public TitusGraphics::IRenderPass` | `class TrianglePass : public ::TitusGfx::IRenderPass` |

## CI 静态扫描

- `Examples/010_UnifiedTriangle/*`（STRICT 模式）中禁止出现：`<vulkan/...>`、`<GL/...>`、`<glad/...>`、`<glfw3.h>`；
  以及不得出现字面 include：`Renderer/`、`RendererCore/`、`RendererVK/`、`Platform/`（任意前缀路径）。
  脚本：[`tools/check_no_backend_headers.bat`](../tools/check_no_backend_headers.bat)。
- 依赖方向单向（脚本：[`tools/check_deps_direction.bat`](../tools/check_deps_direction.bat)，在 `RendererInterface` 项目的 PreBuildEvent 调用）：
  - `RendererCore/*` 禁止 include `Renderer/`、`RendererVK/`、`RendererInterface/`
  - `Renderer/*`     禁止 include `RendererVK/`、`RendererInterface/`
  - `RendererVK/*`   禁止 include `Renderer/`、`RendererInterface/`
  - `Platform/*`     禁止 include `Renderer/`、`RendererCore/`、`RendererVK/`、`RendererInterface/`
这等同于在构建阶段扣住依赖反向违规，使 `RendererInterface → {Renderer, RendererVK, RendererCore, Platform} → RendererCore` 这一单向拓扑被强制保障（需求 9.4 / 10.4）。
