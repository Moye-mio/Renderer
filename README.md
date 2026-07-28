# Titus Renderer

一个 **RHI（Render Hardware Interface）风格的跨后端渲染器**：业务代码面向一套后端无关的
图形抽象编写，可在 **OpenGL**、**Vulkan**（以及 **Null/headless**）后端上以同一份代码运行。
项目定位为渲染架构学习与实验平台。

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
| `Third-Party/` | 随仓库分发的第三方库（imgui、图标字体等） |
| `GraphicSDK/` | 外部依赖（**不随仓库分发**，需自行准备，见下） |
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

### 依赖准备（重要）

`GraphicSDK/` 已被 `.gitignore` 排除，**不包含在本仓库中**。克隆后请先下载并解压：

> **下载 GraphicSDK**：[https://pan.baidu.com/s/1XjYGrGhCO_KSqL7pCUzBrA](https://pan.baidu.com/s/1XjYGrGhCO_KSqL7pCUzBrA)　密码：`1234`，然后将其解压即可（解压后应得到仓库根目录下的 `GraphicSDK/`）。

解压后建议以**管理员权限**运行一次 `GraphicSDK/Graphic.bat`，将依赖路径写入机器级环境变量（`ASSIMP` / `OPENGL` / `GLM` / …），然后重新打开 Visual Studio。

当前 GraphicSDK 包内常见布局（以解压结果为准）：

| 内容 | 说明 |
|---|---|
| `Assimp/` `Eigen/` `glm/` `gli/` `opencv/` `stb_image/` | 常用第三方库 |
| `OpenGL/` | 含 GLFW / glad 等 OpenGL 相关依赖 |
| `boost_1_69/` `CImg/` `SOIL/` | 其它辅助依赖 |
| `Graphic.bat` | 一键设置环境变量 |

> Vulkan 头文件与库由本机 Vulkan SDK 提供，不依赖 GraphicSDK 内的独立 `vulkan/` 目录。

### 运行

示例支持命令行选择后端、线程模式与校验层：

```
--backend=gl|vk|null
--threading=direct|threaded|nonthreaded
--validation=on|off
```

说明：

- 当前 **GL / VK 默认线程模式均为 `Direct`**；可用 `--threading=threaded` 做回归验证。
- `--validation` 主要影响 Vulkan（Debug 默认 on，Release 默认 off）。

示例：

```
Test_000_UnifiedTriangle.exe --backend=vk
000_Deferred_Shading.exe --backend=gl
001_Reflective_shadow_map.exe --backend=vk
```

---

## 许可证

本项目自有代码以 **Apache License 2.0** 授权，详见 [`LICENSE`](./LICENSE)。

第三方组件的许可证与归属声明见 [`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md)。

> 提示：如需以你个人/组织名义主张版权，可在源码文件头或新增 `NOTICE` 文件中加入
> `Copyright <年份> <你的名字/组织>` 声明。
