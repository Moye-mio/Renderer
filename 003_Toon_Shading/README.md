# 003 — Toon Shading

妮露 Diffuse Only 与 Cel-Ramp。

## 编译与启动

在仓库任意目录执行：

- 编译：`003_Toon_Shading\Build\build_debug.bat`
- 运行：`003_Toon_Shading\Build\run_debug.bat`
- 可选：`run_debug.bat --backend=vk`
- 可选：`run_debug.bat --mode=diffuse` 或 `--mode=celramp`（默认 Cel-Ramp）

## 测试条件

| 项 | 值 |
| --- | --- |
| GPU | NVIDIA GeForce RTX 4060 |
| 后端 | OpenGL（`--backend=gl`，Debug 构建） |
| 分辨率 | 1920 × 1152 |
| 场景 | Nilou，AABB `(-0.42, -0.01, -0.35)` ~ `(0.42, 1.63, 0.26)`，贴地后身高约 1.8 m |
| 相机 | 位置 `(0, 1.35, 3.4)`，yaw `-90°`，pitch `-8°`，FOV `35°`，near `0.05`，far `40` |
| 主光 | yaw `8°`，pitch `22°`，ambient `0.08` |
| Ramp | bright `0.52` / grey `0.47` / dark `0.12`，日间行 |

## 包含的算法

- **Diffuse Only**：只采样漫反射贴图，不走半 Lambert / Ramp。
- **Cel-Ramp**：半 Lambert × ilm 采 Shadow Ramp，分亮 / 灰 / 暗三档。

## 算法结果

**Diffuse Only**

![Diffuse Only](Result/DiffuseOnly.png)

**Cel-Ramp**

![Cel Ramp](Result/CelRamp.png)
