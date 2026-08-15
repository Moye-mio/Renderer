# 待办计划：PSO 缓存优化

## 背景

Vulkan / 现代 RHI 中 Graphics/Compute/RT Pipeline（PSO）创建开销高（驱动编译、链接、校验）。本仓在 RHI 层用「desc → handle 去重」避免同配置重复创建；驱动侧还有 `VkPipelineCache`（可选落盘）可加速冷启动编译。这两层常被统称为「PSO 缓存」，但职责不同，需分开评估。

相关架构说明：`Docs/Architecture/23_VK_Step3_Shader_Pipeline.md` §B1、`Docs/Architecture/00_Overview.md`（SamplerCache / PipelineCache）。

## 分层定义


| 层级                      | 含义                                                           | 解决的问题                   |
| ----------------------- | ------------------------------------------------------------ | ----------------------- |
| **L1 运行时去重**            | 同语义 `*PipelineDesc` 只 `Create*Impl` 一次，复用 `PipelineHandle`   | 业务重复请求 / 材质多实例同配置时的无谓创建 |
| **L2 驱动 PipelineCache** | `vkCreateGraphicsPipelines` 等传入 `VkPipelineCache`            | 同进程内后续创建命中驱动内部缓存、缩短编译   |
| **L3 持久化**              | 将 `VkPipelineCache` blob 写磁盘，下次启动 `vkCreatePipelineCache` 灌入 | 冷启动 / 首次进场景卡顿           |




## 现状评估


| 项                                                       | 状态            | 说明                                                                                                                                                                                             |
| ------------------------------------------------------- | ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Graphics：`GraphicsPipelineDesc` → `PipelineHandle` 哈希去重 | **已做**        | `GStateCache.h` 的 `PipelineCache`；`GDevice::CreatePipeline(Graphics)` 先查 `m_pipelineCache`                                                                                                     |
| Sampler 同款去重                                            | **已做**        | `SamplerCache`；与 PSO 无关但同属状态对象缓存                                                                                                                                                               |
| Material 入口走缓存                                          | **已做**        | `Material::GetOrCreatePipeline` → `device.CreatePipeline`                                                                                                                                      |
| 生命周期：Destroy 时从 cache 摘除                                | **已做**        | `GDevice` Destroy(Pipeline) 遍历 `m_pipelineCache` erase                                                                                                                                         |
| Compute：同 desc 去重                                       | **未做**        | `CreatePipeline(Compute)` 注释写明不入 Graphics cache，每次新建                                                                                                                                           |
| RayTracing：同 desc 去重                                    | **未做**        | 与 Compute 同款骨架，无独立 cache                                                                                                                                                                       |
| L1 key 完整性                                              | **部分**        | Graphics hash/eq 覆盖 shader / topology / RS / DS / vertexLayout / blend / rtLayout；**未纳入** `resourceBindings` / `pushConstantRanges` / `debugName`（若两 desc 仅资源布局不同会误命中——需确认业务是否保证与 shader 绑定一致） |
| L2 `VkPipelineCache` 句柄                                 | **未做**        | `VKDevice::CreatePipelineImpl` 直接 `vkCreate*Pipelines`，cache 参数为 `VK_NULL_HANDLE`（需对照实现确认）                                                                                                     |
| L3 磁盘持久化 + UUID 失效                                      | **未做**        | 无 cache 文件读写、无 `VkPhysicalDeviceProperties::pipelineCacheUUID` 校验                                                                                                                              |
| GL 后端                                                   | **N/A / 弱相关** | GL 无 immutable PSO；`GLPipelineEntry` 模拟状态块，L2/L3 不适用；L1 仍有「少建 Program/VAO」价值                                                                                                                   |




## 已做内容（保持即可）

- [x] `RendererCore/GStateCache.h`：`PipelineDescHash` / `PipelineDescEq` + `PipelineCache`
- [x] `GDevice::CreatePipeline(const GraphicsPipelineDesc&)`：查表 → `CreatePipelineImpl` → `emplace`
- [x] `Material::GetOrCreatePipeline` 经设备入口复用
- [x] 单测意图：`DeviceLifecycleTest` 同 desc 两次 `CreatePipeline` 句柄一致
- [x] 文档：`23_VK_Step3_Shader_Pipeline.md` / `10_RendererCore.md` 已记载 Graphics cache 行为



## 未做内容与计划



### P0 — 正确性修补（有误命中风险时优先）

- [ ] **审计 L1 key**：确认 `resourceBindings` / `pushConstantRanges` 是否应进入 hash/eq。若业务可能「同 shader + 同 RS，不同 set 布局」却共用一条 pipeline，当前会错误复用 → 必须补进 key；若约定 bindings 完全由 shader reflection 决定且 desc 总一致，可文档写死约定并加 assert。
- [ ] **Destroy / 多线程**：确认 Worker 路径下 cache 查找与 Destroy 的线程归属与 `GDevice` 现有约定一致（避免并发改 `m_pipelineCache`）。



### P1 — 补齐运行时去重（成本低、与现架构一致）

- [ ] `ComputePipelineCache`：仿 Graphics，对 `ComputePipelineDesc` 做 hash/eq + `unordered_map`；`CreatePipeline(Compute)` 走查表。
- [ ] `RayTracingPipelineCache`（或与 Compute 分表）：对 `RayTracingPipelineDesc`（stages/groups/…）语义哈希；注意 vector 字段与库函数句柄的稳定性。
- [ ] **触发条件**：Compute/RT pass 数量上升、同配置被多处 `CreatePipeline`，或加载路径出现重复创建日志/Tracy 尖峰时实施。



### P2 — 驱动侧 `VkPipelineCache`（几乎零成本接入）

- [ ] Device 初始化创建空 `VkPipelineCache`，`CreatePipelineImpl`（Graphics / Compute / RT）统一传入，而非 `VK_NULL_HANDLE`。
- [ ] Shutdown 时 `vkDestroyPipelineCache`。
- [ ] **触发条件**：开始认真做 VK 启动耗时，或准备上 P3 时作为前置。



### P3 — 持久化（等卡顿证据）

- [ ] 启动：读 cache 文件 → 校验 `pipelineCacheUUID`（及可选：应用/shader 版本号）→ `vkCreatePipelineCache` 带 initialData。
- [ ] 退出或定期：`vkGetPipelineCacheData` 写回磁盘。
- [ ] 失效策略：UUID 不匹配则丢弃；shader 源/SPIR-V 变更策略（版本戳或接受「错 cache 仅变慢」）。
- [ ] **触发条件**：管线数量上百、首次进场景 / 冷启动可测卡顿，且优化加载顺序后仍不够时再做。**当前示例规模不优先。**