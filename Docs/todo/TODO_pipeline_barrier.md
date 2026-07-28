# 待办计划：PipelineBarrier 内存屏障映射优化

## 背景

`RendererGL/GLCommandList.cpp`（约 497-520 行）的 `GLCommandList::PipelineBarrier` 负责把后端无关的 `PipelineBarrierDesc`（`RendererCore/GDescs.h:618`）翻译为 `glMemoryBarrier(GLbitfield)` 的位掩码。

当前策略：只要判定目的端是「着色器读取」（`dstGlobalAccess` 含 `ShaderRead`，或 `dstStage` 为 `FragmentShader` / `VertexShader`），就一次性打开四类 barrier：

```cpp
bits |= GL_TEXTURE_FETCH_BARRIER_BIT;
bits |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
bits |= GL_SHADER_STORAGE_BARRIER_BIT;
bits |= GL_UNIFORM_BARRIER_BIT;
```

若条件不成立，则 fallback 到单个 `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`。

## 现状评估（结论：正确性无缺陷，暂不改）

现有 4 处调用点（均为「同一命令流内先写后读」的显式同步）：

| 文件 | 依赖模式 | src → dst |
|---|---|---|
| `001_Reflective_shadow_map/ShadingWithRSMPass.cpp` | compute 写 storage image → fragment 采样读 | ComputeShader/ShaderWrite → FragmentShader/ShaderRead |
| `Examples/Test_002_RayQueryHello/RayQueryPass.cpp` | 同上 | ComputeShader → FragmentShader |
| `Examples/Test_002_RayQueryHello/RayPipelinePass.cpp` | raygen 写 storage image → fragment 采样读 | ComputeShader(近似) → FragmentShader |
| `Examples/Test_002_RayQueryHello/DynamicScenePass.cpp` | ① TLAS 构建 → rayQuery 读；② compute 写 → fragment 读 | ASBuild/ComputeShader → ComputeShader/FragmentShader |

正确性分析：
- **VK 后端**：显式同步模型，上述 barrier 全部必需，不能删。
- **GL 后端**：涉及 `imageStore` / SSBO 的 incoherent 访问（模式一/三）必需；纯 AS 屏障（模式二）在 GL 下无硬件光追，走 fallback，为无意义但无害的空操作。

因此当前实现无功能缺陷，问题仅在于**性能层面的过度保守**。

## 待办项

- [ ] **（低优先级）按资源类型精确选 bit**：模式一/三消费端为「采样读」，实际只需 `GL_TEXTURE_FETCH_BARRIER_BIT`，无需同时打开 image / SSBO / UBO 三类。可根据 `desc` 中的资源用途信息细化，减少不必要的同步开销。
- [ ] **（低优先级）补全未覆盖的目的阶段**：当前 `dstStage == ComputeShader` 且未标 `ShaderRead` 时会落到 fallback 仅发 image barrier，可能不足以覆盖 SSBO 读取。考虑把 `ComputeShader` 也纳入首个 `if` 条件。
- [ ] **（可选）GL 下识别并跳过纯 AS 屏障**：`srcStage == AccelerationStructureBuild` 且 GL 无光追支持时，可直接跳过（当前 fallback 会多发一个 image barrier）。
- [ ] **（可选）粒度细化**：当前为全局 `glMemoryBarrier`，粒度粗于「只针对目标资源」。若未来有性能需求，可评估 `glMemoryBarrierByRegion` 或按资源追踪的精细屏障。
- [ ] **触发条件**：仅当出现性能瓶颈（barrier 过多导致 GPU 流水线停顿）或新增 compute/SSBO 相关同步 bug 时才实施上述优化。

## 相关定义

- `PipelineStage` / `AccessFlags` 枚举与 `HasFlag`：`RendererCore/GDescs.h:541-594`
- `PipelineBarrierDesc`：`RendererCore/GDescs.h:618`
- GL 实现：`RendererGL/GLCommandList.cpp:497-520`

## 版本前提

- `glMemoryBarrier`：需 **GL 4.2**（image load/store 相关 barrier）。
- 索引/区域版屏障（如按 region）：视具体 API 而定，实施前需确认目标环境支持。
