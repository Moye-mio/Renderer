# 001_Reflective_shadow_map

经典的 **Reflective Shadow Map（RSM）** 间接光照示例：用 GBuffer + RSM Buffer + Compute Shader 计算虚拟点光源（VPL）贡献，最终通过屏幕空间四边形显示。

## 渲染流程

```
SponzaGBufferPass        (GBuffer)        生成相机视角 Albedo / Normal / Position / Depth
RSMBufferPass            (AfterGBuffer)   生成光源视角 Flux / Normal / Position（RSM）
ShadingWithRSMPass       (Lighting)       Compute Shader：用 RSM 采样多个 VPL，写入 ShadingTexture
ScreenQuadPass           (FinalBlit)      把 ShadingTexture 输出到屏幕
```

## 入口与后端

本示例处于 **`renderer_interface_facade` 过渡阶段**：

- `main.cpp` 已通过 `RendererInterface` 提供的 `::TitusGfx::APP::ParseCommandLine`
  解析 `--backend=gl|vk|null` 等统一命令行参数；
- 但 4 个 Pass 内部仍然是 **OpenGL 直调**（`glBindFramebuffer / glDispatchCompute /
  glClear` 等），并且模型加载、相机、UBO、PassScheduler 沿用旧的 `Renderer/`
  （`TitusGraphics::*`）链路；
- 因此本示例 **当前只支持 `--backend=gl`**，运行 `--backend=vk` 会被 `main`
  拒绝并立即退出。

```bash
# 默认 OpenGL：
001_Reflective_shadow_map.exe
# 显式指定：
001_Reflective_shadow_map.exe --backend=gl
# 当前不支持（会立即退出）：
001_Reflective_shadow_map.exe --backend=vk
```

## 模块依赖

```
001_Reflective_shadow_map
   ├─ Renderer (TitusGraphics::* / 旧 GL 链路：窗口/Camera/UBO/PassScheduler)
   ├─ RendererInterface (::TitusGfx::APP / 仅用于命令行解析与后端选择)
   └─ Basic (基础工具)
```

注意：`Pass.cpp` 与 `main.cpp` 均还会 include `Renderer/Interface.h`、
`<GL/glew.h>` 等遗留头，因此本工程暂不参与 `tools/check_no_backend_headers.bat`
的"业务侧后端头静态扫描"白名单（参见 RendererInterface CI 配置）。

## 后续阶段（M7+）

把 4 个 Pass 真正翻成 `::TitusGfx::IRenderPass + RenderCommandList`，依赖以下
`RendererCore` 能力先行就绪：

- [ ] Compute Pass：`BeginComputePass / Dispatch / MemoryBarrier`
- [ ] 多 RT FrameBuffer：通过 `IGDevice::CreateRenderTarget` 创建多张
      `R32G32B32A32_SFLOAT` + 一张 `D32_SFLOAT`，并在 `RenderPassBeginInfo`
      中按 LoadOp/StoreOp 配置
- [ ] Image Bind / Storage Texture（用于 Compute 写入）
- [ ] `GpuModel + AssetGpuUploader` 替换 `Renderer/Model.cpp`
- [ ] `Camera + UBO4ProjectionWorld` 上提至 `RendererCore`
- [ ] Shader 资源同时产出 `.glsl + .spv`（`Shader/CompileShaders.bat`）

完成上述前置任务后，本工程即可彻底脱离 `TitusGraphics::*` 旧 API，**与
`Examples/010_UnifiedTriangle` 一样仅依赖 `RendererInterface`**，并在两个
后端下产出一致画面。
