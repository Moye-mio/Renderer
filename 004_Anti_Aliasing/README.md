# 004 — Anti-Aliasing

同一 Sponza 场景下对比抗锯齿方案。当前接入无 AA 基线、硬件 MSAA、FXAA 与 TAA；SMAA 尚未实现。

## 编译与启动

在仓库任意目录执行：

- 编译：`004_Anti_Aliasing\Build\build_debug.bat`
- 运行：`004_Anti_Aliasing\Build\run_debug.bat`
- 可选：`run_debug.bat --backend=vk`
- 可选：`run_debug.bat --mode=msaa` / `--mode=fxaa` / `--mode=taa`（默认 None）

## 包含的算法

- **None**：无抗锯齿，Sponza 漫反射 ×（环境 + Lambert），画到默认 backbuffer。
- **MSAA**：前向画到离屏 multisample RT，硬件 resolve 到 1-sample 后再拷回 backbuffer。采样数可在 overlay 里选 2x / 4x / 8x。MSAA 与 Deferred 不兼容（per-sample 着色代价高），本示例走前向路径。
- **FXAA**：前向画到离屏 LDR，再按 Lottes FXAA 3.11 Quality 做 luma 边缘检测与沿边混合，直接写回 backbuffer。无历史帧。Overlay 可调 Subpix、Edge Threshold、Edge Threshold Min。
- **TAA**：Halton(2,3) 子像素 jitter 画颜色 + RG16F velocity，按上一帧 view-proj 重投影 HDR history，邻域 AABB / variance clip 压鬼影后写回双缓冲。Overlay 可调 Feedback、Clamp、Jitter Scale。
- SMAA：待接入。

## 算法结果

![FXAA](Result/FXAA.png)

![MSAA](Result/MSAA.png)

![TAA](Result/TAA.png)
