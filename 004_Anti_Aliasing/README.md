# 004 — Anti-Aliasing

同一 Sponza 场景下对比抗锯齿方案。当前接入无 AA 基线与硬件 MSAA；FXAA / SMAA / TAA 尚未实现。

## 编译与启动

在仓库任意目录执行：

- 编译：`004_Anti_Aliasing\Build\build_debug.bat`
- 运行：`004_Anti_Aliasing\Build\run_debug.bat`
- 可选：`run_debug.bat --backend=vk`
- 可选：`run_debug.bat --mode=msaa`（默认 None）

## 包含的算法

- **None**：无抗锯齿，Sponza 漫反射 ×（环境 + Lambert），画到默认 backbuffer。
- **MSAA**：前向画到离屏 multisample RT，硬件 resolve 到 1-sample 后再拷回 backbuffer。采样数可在 overlay 里选 2x / 4x / 8x。MSAA 与 Deferred 不兼容（per-sample 着色代价高），本示例走前向路径。
- FXAA / SMAA / TAA：待接入。
