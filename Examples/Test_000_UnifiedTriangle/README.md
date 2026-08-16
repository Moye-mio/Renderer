# Test_000_UnifiedTriangle

后端无关渲染层的最小对照示例：**同一份源代码** 通过命令行参数
`--backend=gl` 或 `--backend=vk` 切换 OpenGL / Vulkan 后端，渲染同一个三角形。

## 用法

```
Test_000_UnifiedTriangle.exe --backend=vk    # 使用 Vulkan 后端
Test_000_UnifiedTriangle.exe --backend=gl    # 使用 OpenGL 后端
```

不带参数时默认 `--backend=vk`。

## 文件结构

```
Examples/Test_000_UnifiedTriangle/
├─ TrianglePass.{h,cpp}     业务 Pass：继承 ::TitusRHI::IRenderPass，仅 include RendererInterface/TitusGfxPass.h
├─ main.cpp                 入口：解析 --backend，走 APP::InitApp / AddPass / UpdateApp
├─ Shader/
│  ├─ triangle.vert         Vulkan 用源（编译为 .spv）
│  ├─ triangle.frag
│  ├─ triangle.vert.spv     由 PreBuild 阶段的 CompileShaders.bat 产出
│  ├─ triangle.frag.spv
│  ├─ triangle.vert.glsl    OpenGL 直接消费（glShaderSource）
│  ├─ triangle.frag.glsl
│  └─ CompileShaders.bat
└─ Test_000_UnifiedTriangle.vcxproj
```

## 禁止包含的头文件清单

本工程必须 **只 include `RendererInterface/*.h`（业务入口 `TitusGfxPass.h` / `TitusGfx.h`）与 `Basic`**，禁止出现任何后端头：

| 头文件 | 后端 |
| --- | --- |
| `<vulkan/...>` | Vulkan |
| `<GL/...>`     | OpenGL |
| `<glad/...>`   | OpenGL |
| `<GLFW/glfw3.h>` / `"glfw3.h"` | GLFW |
| `Renderer/*` / `RendererGL/*` / `RendererVK/*` / `RendererCore/*` / `Platform/*` | 后端/内部实现 |

PreBuild 阶段会自动调用 [`Tools/check_no_backend_headers.bat`](../../Tools/check_no_backend_headers.bat)
扫描本目录；一旦命中以上头文件，构建立即失败。（`RendererCore/*` 亦不直接 include——业务统一经 `RendererInterface/TitusGfxPass.h` 聚合入口。）

## 相关示例

- `Test_001_VkTriangle/`：同样继承 `::TitusRHI::IRenderPass` 的统一 Pass，默认 Vulkan
  后端。**已完成**从旧 `IVkRenderPass` 到 `RendererCore::IRenderPass` 的迁移，验证了
  单后端示例迁移到统一基类后 GL/VK 双后端输出一致。
- `Test_002_RayQueryHello/`：Vulkan 光追最小示例（rayQuery / RT pipeline / 动态 TLAS）。
- `000_Forward_Deferred_ForwardPlus` / `001_Reflective_shadow_map`：更完整的多 Pass 示例，同样全部
  基于 `::TitusRHI::IRenderPass`，均可 `--backend=gl|vk` 双后端运行。

> 说明：旧的后端专用 Pass 基类（`RendererGL` 全局 `IRenderPass`、`RendererVK` 的
> `IVkRenderPass`）及各自的独立调度器已随旧路径清退删除；当前所有业务 Pass 统一走
> `RendererCore::IRenderPass` + `PassScheduler`。
