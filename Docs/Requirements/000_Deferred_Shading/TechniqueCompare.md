# 000_Deferred_Shading — 技术对比需求

本地文档，不入库。对照实现：`000_Deferred_Shading/` 下的 TechniqueContext、Forward Pass 与 ImGui 面板。

## 1. 背景与目标

工程 000 原先只有一条固定路径：

1. `SponzaGBufferPass` 把 Sponza 写入 Albedo / Normal-VS / Position-VS / Depth
2. `DeferredLightingPass` 全屏采样 G-Buffer，对 5 个点光做视空间 Blinn-Phong，输出到 backbuffer

目标：在**同一场景、同一套灯、同一套 BRDF** 下，用 ImGui 切换 Deferred 与 Forward，并用默认 Overlay 的 FPS / 帧时做对比。

## 2. 非目标

- 不改 `IRenderPass` / `PassScheduler` 内部遍历逻辑
- 不在切换时 Destroy/重建 Pipeline（资源保持，只换调度列表）
- 不做 Tiled / Clustered / Light Pre-Pass
- 不抽公共 shader include（允许 Forward FS 与 Deferred FS 重复光照循环）
- 不加灯数滑条、不做窗口 resize 重建 G-Buffer

## 3. 架构

```
TechniqueContext
├── mode                    当前算法（ImGui 只改这个）
├── shared                  对比必须一致的输入（灯 / BRDF 常数 / UBO 布局）
├── deferred                仅 Deferred 认识（G-Buffer debug 视图）
└── forward                 仅 Forward 认识（V1 空占位）
```

- Context 只持 CPU 参数，不持 Texture / Pipeline。
- G-Buffer handle 仍由 GBuffer Pass `RegisterSharedData`，Lighting Pass 从黑板读取。
- 三个 Pass 启动时都 `AddPass` + `Init`（生命周期登记）。
- `APP::SetScheduledPasses` 按 mode 替换调度列表：Deferred 只挂 GBuffer+Lighting，Forward 只挂 Forward。
- `Record` 仍按 `mode` 早退，作为调度与 mode 不一致时的兜底。
- 切 Forward 时调度器不再遍历 GBuffer，不会写三张 RGBA32F。

## 4. 功能

| 项 | 说明 |
|---|---|
| ImGui 面板 `Shading Technique` | Radio：Deferred / Forward；文本：灯数；仅 Deferred 时显示 Debug view combo |
| 调度互斥 | Deferred：调度器仅 GBuffer + Lighting；Forward：仅 ForwardShadingPass |
| Pass 早退 | 兜底：`mode` 不匹配时 `Record` 直接 return |
| Forward | 几何直接画到 backbuffer，开 depth test/write，同一套视空间 Blinn-Phong |
| Deferred debug | Final / Albedo / Normal / Position |
| 默认 Overlay | 不调用 `IMGUI::SetUserCallback`，保留 Renderer Info（FPS） |

## 5. 公平对比约定

- 灯：`shared.lights`（位置 / 半径 / 颜色 / 强度 / 数量）
- BRDF：环境 `albedo * ambient(0.08)`，高光指数 `shininess(32)`，半径平方衰减
- 坐标系：视空间，相机在原点，`V = normalize(-fragPosVS)`
- 背景：无几何处 `(0.02, 0.02, 0.03)`（Deferred 看法线长度；Forward 用 Clear）

## 6. 验收

- 切 Forward 后画面仍是 Sponza + 5 盏彩色点光；Tracy 上不应再出现 `Pass:GBuffer` / `Pass:Lighting` 区（调度器里没有这两个 Pass）
- 切回 Deferred 无需重启，G-Buffer 资源仍在
- 两算法默认 Final 视图下灯光与公式一致（允许 overdraw / 插值带来的像素级差异）
- Deferred Debug view 能分别看出 Albedo / Normal / Position
- `--backend=gl` 与 `--backend=vk` 均可切换
