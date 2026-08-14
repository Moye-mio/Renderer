# Titus Renderer

一个 **RHI（Render Hardware Interface）风格的跨后端渲染器**：业务代码面向一套后端无关的
图形抽象编写，可在 **OpenGL**、**Vulkan**（以及 **Null/headless**）后端上以同一份代码运行。
项目定位为渲染架构学习与实验平台，可用 [Tracy](https://github.com/wolfpld/tracy) 做 CPU 帧路径与 Pass 调度的性能评估。

> 对外 API 命名空间为 **`TitusRHI`**（头文件前缀为 `TitusGfx*`，例如 `TitusGfx.h`）。
> 核心抽象参考了业界跨后端渲染器（UE RHI / bgfx 等）的通用设计：后端无关设备接口 +
> 可插拔后端，以及可切换的线程模型（默认 Direct，可选 Threaded）。

---

## 架构分层

```
业务 Pass / Examples / 000_* / 001_*
      │  只 #include RendererInterface/*
      ▼
RendererInterface   门面：TitusRHI::APP / WINDOW_KEYWORD / CAMERA /
                    RESOURCE_MANAGER / INPUT_MANAGER / …
      ▼
RendererCore        后端无关抽象：IGDevice / RenderCommandList / GHandle /
                    GDescs / GEnums / PassScheduler / Material / GpuModel
   ┌──┴──┐
RendererGL   RendererVK     可插拔后端（OpenGL / Vulkan）
      │
AssetLoader（TitusAsset，纯 CPU） + Platform（GLFW / IWindow） + Basic
```

- **唯一后端分发点**：`RendererInterface/GDeviceFactory.cpp`（同时触达 GL / VK）。
- **帧循环**：`APP::UpdateApp` → `PassScheduler`：
  `BeginFrame → AcquireCommandList → Pass.Record → Submit → Present`。

关键设计：

- **类型安全句柄** `GHandle<Tag>`：业务不接触裸后端对象指针。
- **描述符驱动**：资源/管线经 `*Desc`（POD + 自定义枚举）创建，抽象层禁止出现 `VkXxx` / `GLenum`。
- **统一帧循环**：两后端共用同一套 Pass 调度路径。
- **架构护栏**（构建期脚本）：
  - `tools/check_no_backend_headers`：业务目录禁止 include 后端/内部头。
  - `tools/check_deps_direction`：强制模块依赖单向。

更细的设计说明见 [`Docs/Architecture/00_Overview.md`](./Docs/Architecture/00_Overview.md)
与各模块目录下的 `README.md`。

---

## 目录结构

| 目录 | 说明 |
|---|---|
| `RendererInterface/` | 门面层，业务唯一对外入口（`TitusRHI::*`） |
| `RendererCore/` | 后端无关的 RHI 抽象 |
| `RendererGL/` | OpenGL 后端 |
| `RendererVK/` | Vulkan 后端 |
| `AssetLoader/` | 纯 CPU 资产加载（模型/纹理），零 GPU 依赖 |
| `Platform/` | 窗口抽象的 GLFW 实现 |
| `Basic/` | 基础工具（Logger / Math / Singleton 等） |
| `Examples/` | 跨后端最小示例（见下） |
| `000_Deferred_Shading/` | 延迟着色示例（Sponza + 多点光） |
| `001_Reflective_shadow_map/` | RSM 间接光示例 |
| `Docs/` | 设计文档与 TODO |
| `tools/` | 架构护栏与着色器辅助脚本 |
| `Third-Party/` | 第三方库（imgui、glm、gli、stb、Assimp、GLFW/GLEW、Tracy 等） |
| `Model/` `Fonts/` | 运行时资源 |

> 磁盘上可能还有 `002_*` 等旧 OpenGL 实验目录，**未纳入** `TitusGLRenderer.sln`，请以解决方案中的工程为准。

---

## 示例工程

打开 `TitusGLRenderer.sln` 后，主要可运行工程：

| 工程 | 说明 | 默认后端 |
|---|---|---|
| `Examples/Test_000_UnifiedTriangle` | 最小跨后端三角形；PreBuild 跑护栏脚本 | `vk` |
| `Examples/Test_001_VkTriangle` | 基于 `IRenderPass` 的三角形（亦可切 GL） | `vk` |
| `000_Deferred_Shading` | GBuffer → 多点光 Blinn-Phong；含飞行相机与 ImGui overlay | `gl` |
| `001_Reflective_shadow_map` | GBuffer → RSM → Compute VPL → ScreenQuad | `gl` |

---

## 构建

### 环境

- Windows + Visual Studio（打开根目录 `TitusGLRenderer.sln`）。
- 使用 Vulkan 后端时需安装 [Vulkan SDK](https://vulkan.lunarg.com/)，并确保 `VULKAN_SDK` 已设置。

### 依赖准备

克隆时请带上 **Tracy 子模块**（性能分析）。其余第三方库（glm / gli / stb / Assimp / GLFW / GLEW）均在 `Third-Party/`，随仓库分发。路径由根目录 `Directory.Build.props` 解析，**不必**再设 `ASSIMP` / `OPENGL` / `GLM` 等环境变量。

```powershell
git clone --recurse-submodules https://github.com/Moye-mio/TRenderer.git
```

已有克隆、尚未拉子模块时：

```powershell
git submodule update --init --recursive
```

| 位置 | 内容 |
|---|---|
| `Third-Party/tracy/` | Tracy Profiler 客户端（git submodule） |
| `Third-Party/glm/` | GLM 0.9.8.3（header-only） |
| `Third-Party/gli/` | GLI 0.8.2（DDS/KTX） |
| `Third-Party/stb/include/` | stb_image / stb_image_write |
| `Third-Party/Assimp/` | Assimp 头文件 + `lib/x64` + `bin/x64` |
| `Third-Party/OpenGL/` | GLFW / GLEW（`include`、`lib/x64`、`bin/x64`） |

> Vulkan 头文件与库由本机 Vulkan SDK 提供，不在本仓库中。

### 运行

经 `APP::ParseCommandLine` 的公共参数（`on|off` 亦接受 `true|false|1|0`）：

```
--backend=gl|vk|null
--threading=direct|threaded|nonthreaded
--validation=on|off
--screenshot-at=<seconds>
--screenshot-dir=<path>
--quit-after-screenshot=on|off
```

说明：

- 当前 **GL / VK 默认线程模式均为 `Direct`**；可用 `--threading=threaded` 做回归验证。
- `--validation` 主要影响 Vulkan（Debug 默认 on，Release 默认 off）。
- `--screenshot-at`：进程启动后经过指定墙钟秒数自动截一张 PNG（含 ImGui Overlay）。未指定则不自动截。
- `--screenshot-dir`：输出目录；默认 `$(SolutionDir)<应用名>/results/`。文件名为 `shot_<gl|vk>_<YYYYMMDD_HHMMSS>.png`。
- `--quit-after-screenshot`：自动截图成功后是否退出，默认 `on`。手动在 ImGui「Renderer Info」里点 **Capture Screenshot** 不退出。

示例：

```
Test_000_UnifiedTriangle.exe --backend=vk
000_Deferred_Shading.exe --backend=gl
001_Reflective_shadow_map.exe --backend=vk --screenshot-at=2 --screenshot-dir=results
```

---

## 性能分析（Tracy）

用 [Tracy](https://github.com/wolfpld/tracy) 评估 CPU 侧帧循环、Pass 调度与设备提交耗时。业务代码只通过 `Basic/TracySupport.h` 插桩，不要直接 include Tracy 头。

- **Debug 默认开启**，**Release 默认关闭**（测接近无插桩的帧率请用 Release，或 `/p:TitusTracyEnable=false` 后全量重编）。
- 开启时带 `TRACY_ON_DEMAND`：Tracy Profiler **未连接**时不往队列写事件。
- 运行示例后打开 Tracy Profiler，**Connect** 到进程即可看时间线（`FrameMark`、Pass Zone、VK 等待等）。
- ImGui「Renderer Info」里有 **Tracy Capture** 开关：已连接时仍可暂停采集，便于对比开关采集的帧率。

命令行覆盖：

```text
msbuild ... /p:TitusTracyEnable=false   # Debug 下关掉 Tracy
msbuild ... /p:TitusTracyEnable=true    # Release 下打开 Tracy
```

更完整的开关语义、插桩地图与陷阱见 [`Docs/Architecture/50_Tracy.md`](./Docs/Architecture/50_Tracy.md)。

---

## 许可证

本项目自有代码以 **Apache License 2.0** 授权，详见 [`LICENSE`](./LICENSE)。

第三方组件的许可证与归属声明见 [`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md)。

> 提示：如需以你个人/组织名义主张版权，可在源码文件头或新增 `NOTICE` 文件中加入
> `Copyright <年份> <你的名字/组织>` 声明。
