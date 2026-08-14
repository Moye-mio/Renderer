# RendererInterface（门面层 / Facade）

> 渲染层对外**唯一**入口。外部模块只 include 本目录下的 `TitusGfx*.h`，
> **不得**直接 include `RendererCore/`、`RendererGL/`、`RendererVK/`、`Platform/` 中的任何头文件。
>
> 头文件前缀为 `TitusGfx*`，对外命名空间为 **`TitusRHI`**（不是 `TitusGfx`）。

---

## 设计要点

- 命名空间 `TitusRHI::APP / WINDOW_KEYWORD / COMPONENT_CONFIG / RESOURCE_MANAGER / INPUT_MANAGER / CAMERA / IMGUI / OVERLAY`
- 后端选择：`--backend=gl|vk|null`（命令行）或 `APP::SetBackend(...)`
- 线程模式：`--threading=direct|threaded|nonthreaded` 或 `APP::SetThreadingMode(...)`
- **默认线程模式：GL / VK 均为 `Direct`**（`Threaded` 对 Compute / Descriptor / AS 尚未完全补齐，可用 `--threading=threaded` 做回归）
- Validation：`--validation=on|off`（主要影响 Vulkan）
- 截图：`--screenshot-at` / `--screenshot-dir` / `--quit-after-screenshot`，或 `APP::CaptureScreenshot*`
- 业务 Pass：`#include "RendererInterface/TitusGfxPass.h"` 后继承 `TitusRHI::IRenderPass`
- 最小示例 `Examples/Test_000_UnifiedTriangle` **仅** include `RendererInterface/*.h`、**仅**链接 `RendererInterface.lib`；同一份源码以 `--backend=gl|vk` 启动

## 依赖方向

```
RendererInterface ──► { RendererGL, RendererVK, RendererCore, Platform, AssetLoader }
        │
        └── 只有 GDeviceFactory.cpp 同时引用 GLDevice / VKDevice
```

CI 脚本 `Tools\check_deps_direction.bat` 在本工程 PreBuildEvent 中强制上述方向。

## 关键文件

| 文件 | 职责 |
|---|---|
| `TitusGfx.h` / `.cpp` | 对外门面：`APP` 生命周期、后端/线程装配、窗口/相机/输入/黑板 |
| `TitusGfxPass.h` | Pass 实现侧聚合入口（转发 RendererCore 核心头） |
| `TitusGfxEnums.h` | `GBackend` / `GThreadingMode` / `ERenderPassEvent` 转发 |
| `TitusGfxImGui.*` / `TitusGfxOverlay.*` | ImGui 门面与 Overlay（含 Tracy Capture / Screenshot） |
| `GDeviceFactory.*` | 唯一后端分发点 |

## CI 静态扫描

- 业务示例（STRICT）禁止：`<vulkan/...>`、`<GL/...>`、`<glad/...>`、`<glfw3.h>`，以及字面 include `RendererGL/`、`RendererCore/`、`RendererVK/`、`Platform/`。
  脚本：[`Tools/check_no_backend_headers.bat`](../Tools/check_no_backend_headers.bat)。
- 依赖方向：[`Tools/check_deps_direction.bat`](../Tools/check_deps_direction.bat)。

更细说明见 [`Docs/Architecture/40_Interface.md`](../Docs/Architecture/40_Interface.md)。
