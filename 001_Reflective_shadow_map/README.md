# 001_Reflective_shadow_map

经典的 **Reflective Shadow Map（RSM）** 间接光照示例：用 GBuffer + RSM Buffer + Compute Shader 计算虚拟点光源（VPL）贡献，最终通过屏幕空间四边形显示。

业务侧仅依赖 `RendererInterface`（命名空间 **`TitusRHI`**），模型经 `AssetLoader` → `APP::UploadGpuModel` 上传，**GL / VK 双后端**可运行。

## 渲染流程

```
SponzaGBufferPass        (GBuffer)        生成相机视角 Albedo / Normal / Position / Depth
RSMBufferPass            (AfterGBuffer)   生成光源视角 Flux / Normal / Position（RSM）
ShadingWithRSMPass       (Lighting)       Compute：用 RSM 采样多个 VPL，写入 ShadingTexture
ScreenQuadPass           (FinalBlit)      把 ShadingTexture 输出到屏幕
```

## 运行

```bash
001_Reflective_shadow_map.exe                  # 默认 OpenGL
001_Reflective_shadow_map.exe --backend=gl
001_Reflective_shadow_map.exe --backend=vk     # Vulkan（Compute + Descriptor + glslang 在线编译）
```

内置飞行相机（`CAMERA::EnableBuiltinFlyCamera`）：WASD / QE 平移，右键拖拽旋转；ImGui「Renderer Info」面板含 FPS / Backend / Tracy Capture / Screenshot。

## 模块依赖

```
001_Reflective_shadow_map
   ├─ RendererInterface（TitusRHI::APP / IRenderPass / CAMERA / IMGUI …）
   ├─ AssetLoader（TitusAsset：Sponza CPU IR）
   └─ Basic
```

Pass 继承 `TitusRHI::IRenderPass`，经 `TitusGfxPass.h` 聚合入口；不直接 include `RendererGL/` / `RendererVK/` / `RendererCore/`。

## 相关

- 更小的跨后端三角形：`Examples/Test_000_UnifiedTriangle`
- 光追最小示例：`Examples/Test_002_RayQueryHello`
- 架构说明：`Docs/Architecture/00_Overview.md`
