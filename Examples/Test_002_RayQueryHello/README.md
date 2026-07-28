# 0xx_RayQueryHello — Vulkan 光线追踪端到端示例

验证 `RendererVK` 光线追踪通路的最小示例，覆盖两条路线：

- **路线 A（Ray Query / P0，默认）**：compute 着色器内 `rayQuery` 求交。
- **路线 B（Ray Tracing Pipeline / P1，`--rtpipeline`）**：独立 RT 管线 +
  SBT + `TraceRays` 的 raygen→miss/hit 流程。
- **动态场景（P2，`--dynamic`）**：`RayTracingManager`
  管理多个引用同一 BLAS 的 instance（BLAS 去重），每帧移动 instance 并
  refit TLAS（增量更新），再用 rayQuery 渲染。

## 闭环

路线 A：
```
三角形顶点缓冲 → 构建 BLAS → 构建 TLAS
   → compute 着色器内 rayQuery 逐像素求交 → 写入 StorageImage
   → 全屏三角形采样 StorageImage → 显示
```

路线 B：
```
三角形 BLAS → TLAS → RT 管线（raygen/miss/closesthit + SBT）
   → TraceRays 写 StorageImage → 全屏三角形采样显示
```

两条路线命中三角形时均以**重心坐标**着色（红/绿/蓝渐变），未命中显示深蓝背景。

## 分层约束

- 源码**仅** include `RendererInterface/TitusGfxPass.h`（门面聚合 `::TitusGfx`
  后端无关抽象），**不接触任何 `VkXxx` 或 `RendererVK/` 头**，通过
  `tools/check_no_backend_headers.bat` 静态扫描。
- 光追资源全部经 `IGDevice::CreateAccelerationStructure` /
  `RenderCommandList` 抽象接口创建与录制。

## 着色器

`Shader/*.glsl` 为 GLSL 源码，**无需预编译 `.spv`**：VK 后端
`VKDevice::CreateShaderImpl` 会按 magic word 嗅探，对 GLSL 文本自动走
glslang 在线编译（目标 SPIR-V 1.5，支持 `GL_EXT_ray_query`）。

- `raytrace.comp.glsl` — rayQuery compute（binding0=StorageImage, binding1=TLAS）
- `raygen.rgen.glsl` / `miss.rmiss.glsl` / `closesthit.rchit.glsl` — RT 管线组
- `blit.vert.glsl` / `blit.frag.glsl` — 全屏三角形显示

## 运行

默认 Vulkan 后端、Direct 渲染模式（Threaded 路径当前不支持 Compute/AS）：

```
0xx_RayQueryHello.exe               # 路线 A：Ray Query
0xx_RayQueryHello.exe --rtpipeline  # 路线 B：Ray Tracing Pipeline
0xx_RayQueryHello.exe --dynamic     # P2：动态场景（AS 管理层 + 每帧 refit）
```

路线 B 额外要求设备支持 `VK_KHR_ray_tracing_pipeline`
（`GetCaps().supportsRayTracingPipeline == true`）。

## 降级行为

在**不支持** KHR 光追的 GPU 上，`GetCaps().supportsRayTracing == false`，
示例检测后优雅提示并仅清屏（红底），不崩溃（需求 13.3）。

## 依赖

- 定义 `RENDERER_ENABLE_RAY_TRACING` 的 `RendererVK`（默认已开启）
- 支持 `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` 的 GPU 与驱动
