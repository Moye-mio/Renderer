# 待办计划：后续示例工程路线（卡通渲染 / 抗锯齿 / 光线追踪）

## 背景

本仓的示例工程遵循同一套模式：**一个主题 + 多种算法实现 + ImGui 运行时切换 + 截图对比**，业务层只 include `RendererInterface/`*，通过 `Tools/check_no_backend_headers` 护栏保证不接触后端头。

已有：


| 工程                                   | 主题                                    | 默认后端 |
| ------------------------------------ | ------------------------------------- | ---- |
| `000_Forward_Deferred_ForwardPlus`   | 着色管线对比（Forward / Deferred / Forward+） | `gl` |
| `001_Reflective_shadow_map`          | RSM 间接光                               | `gl` |
| `002_Order_Independent_Transparency` | OIT（WBOIT / Fourier OIT）· 进行中         | `gl` |
| `Examples/Test_002_RayQueryHello`    | VK 光追最小闭环（rayQuery / RT 管线 / AS 管理）   | `vk` |


本文规划接下来三个主题工程的编号、算法清单与 RHI 前置项。

## 总览


| 计划工程                     | 主题         | 主要算法                     | 后端          | RHI 改动量              |
| ------------------------ | ---------- | ------------------------ | ----------- | -------------------- |
| `003_Toon_Shading`       | 卡通渲染 / NPR | Cel-Ramp、描边、Rim、Hatching | `gl` + `vk` | 小（stencil 描边路线除外）    |
| `004_Anti_Aliasing`      | 抗锯齿对比      | FXAA / SMAA / TAA / MSAA | `gl` + `vk` | 前三者无；MSAA 需补 resolve |
| `005_Ray_Traced_Effects` | 混合光追       | RT 阴影 / RTAO / RT 反射     | `vk` only   | 中（几何与材质寻址）           |


建议顺序 **003 → 004 → 005**：003 几乎零基建、可先把 NPR 素材与 LUT 通路跑通；004 的 TAA 会顺带补齐 jitter / motion vector / history 三件套，这三件套正好是 005 做光追降噪与时域累积的前置。

## RHI 前置能力评估

盘点结论（`RendererCore/GDescs.h`、`RendererGL/GLCommandList.cpp`、`RendererVK/VKDevice.cpp`）：


| 能力                                     | 状态          | 说明                                                                                                        |
| -------------------------------------- | ----------- | --------------------------------------------------------------------------------------------------------- |
| `CullMode` / `FrontFace`               | **已支持**     | 两后端均已下发，背面外扩描边可直接做                                                                                        |
| 1D/2D LUT 纹理采样                         | **已支持**     | Ramp / SMAA LUT 走普通 `Sampled` 纹理即可                                                                        |
| 多 RT（MRT）                              | **已支持**     | motion vector 可作为 GBuffer 额外附件                                                                            |
| Compute                                | **已支持**     | VK 完整；GL 后端后处理亦可退回全屏三角形                                                                                   |
| 光追（AS / rayQuery / RT 管线 / TLAS refit） | **已支持（VK）** | `RayTracingManager`、`Test_002_RayQueryHello` 已验证                                                          |
| `samples`（MSAA）字段                      | **部分**      | `TextureDesc::samples` / `RenderTargetLayout::samples` 已进 VK image / pipeline / renderpass；**GL 后端完全未处理** |
| MSAA Resolve                           | **未做**      | 无 resolve attachment、无 `vkCmdResolveImage`、GL 无 multisample RBO + `glBlitFramebuffer`                     |
| Stencil 完整状态                           | **未做**      | `DepthStencilState` 只有 `stencilTestEnable` 一个 bool，无 ref / mask / `StencilOpState`                        |
| 深度偏移（polygon offset / depth bias）      | **未做**      | `RasterizerState` 无 `depthBias`* 字段                                                                       |
| Bindless / 描述符数组                       | **待确认**     | 影响 005 在 hit shader 里索引任意 mesh 的顶点与材质                                                                     |


---



## 003_Toon_Shading（卡通渲染）

详细需求与任务已拆到：

- 设计：[`Docs/Requirements/000_Deferred_Shading/003_Toon_Shading_Design.md`](../Requirements/000_Deferred_Shading/003_Toon_Shading_Design.md)
- 任务：[`Docs/Requirements/000_Deferred_Shading/003_Toon_Shading_Tasks.md`](../Requirements/000_Deferred_Shading/003_Toon_Shading_Tasks.md)

摘要：场景用妮露（`Model/Nilou/`，源 FBX；运行时加载 ufbx 转出的 T-pose OBJ），不用 Sponza。NPR 贴图按材质名查表绑定，不改 Core 的 PBR `TextureSlot`。里程碑 M1 Diffuse 出图 → M2 Cel-Ramp → M3 背面外扩描边 → M4 脸 SDF + Rim → M5 后处理描边 / 可选项。Stencil 描边仍被 RHI 阻塞，不进本期。

---



## 004_Anti_Aliasing（抗锯齿）



### 目标

同场景下切换 AA 方案，对比**边缘质量 / 运动稳定性 / 耗时**三个维度。场景需要刻意制造锯齿：细栏杆、高频棋盘格纹理、缓慢移动的相机。

### 分阶段



#### 阶段 1 — FXAA（零基建，先落地）

- [ ] 全屏后处理，输入 LDR + luma，无历史帧、无 RHI 改动，两后端直接可跑。
- [ ] 作为后续方案的基线与性能参照。



#### 阶段 2 — SMAA 1x

- [ ] 三趟：边缘检测 → 混合权重（采样 AreaTex / SearchTex）→ 邻域混合。
- [ ] LUT 走 `Third-Party/gli` 的 DDS 加载路径，注意采样器需 `Clamp` + 精确的 `Point`/`Linear` 搭配。



#### 阶段 3 — TAA（本工程的重点，也是 005 的前置）

- [ ] **相机 jitter**：投影矩阵按 Halton(2,3) 序列做子像素偏移，注意同一帧的 jitter 要同时用于渲染与 motion vector 计算。
- [ ] **Motion Vector**：GBuffer 增开一张 `RG16F`，写 `当前 NDC - 上一帧 NDC`；需要在场景层持有上一帧的 view-proj 与（若有动态物体）上一帧 model 矩阵。
- [ ] **History 双缓冲**：两张 HDR 颜色 RT 轮转，读一张写一张。注意 VK 侧的 layout 转换与 GL 侧的 FBO 绑定切换。
- [ ] **Reprojection + 抑制鬼影**：邻域 AABB clamp / variance clipping，配合 disocclusion 判定。
- [ ] ImGui 暴露 feedback 系数、clamp 模式开关，便于现场演示鬼影与糊化的取舍。



#### 阶段 4 — MSAA（**被 RHI 阻塞**）

前三阶段都在业务层，MSAA 是唯一需要动 RHI 的：

- [ ] **抽象层**：`RenderPassBeginInfo` / `RenderTargetLayout` 增加 resolve target 表达（或提供显式 `CmdResolveTexture`），并明确 `samples > 1` 的纹理不可直接 `Sampled`。
- [ ] **VK 后端**：renderpass attachment 补 `pResolveAttachments`（或用 `vkCmdResolveImage`），`VkImageCreateInfo::samples` 已通、需补 usage 与 layout 校验。
- [ ] **GL 后端**：目前对 `samples` **完全没有处理**——需要 multisample RBO / `glTexImage2DMultisample` + `glBlitFramebuffer` 做 resolve。
- [ ] **限制说明**：MSAA 与 Deferred 不兼容（per-sample 着色代价高），本示例中 MSAA 分支应走 Forward 路径，文档里写明这个约束。
- [ ] **触发条件**：阶段 1-3 跑完、确实要把 MSAA 纳入对比时再启动；否则这条 RHI 改动不值得先行投入。



### 验证

- [ ] 固定机位 + 固定帧序，各方案各出一张 `--screenshot-at` 截图，同区域放大拼图对比。
- [ ] Tracy 记录各 AA Pass 的 CPU 耗时；GPU 耗时若需精确，另立 GPU timestamp 的待办。

---



## 005_Ray_Traced_Effects（光线追踪）



### 现状

`Examples/Test_002_RayQueryHello` 已跑通端到端最小闭环：BLAS/TLAS 构建、rayQuery compute、RT 管线 + SBT、`RayTracingManager` 的 BLAS 去重与 TLAS refit，且 `GetCaps()` 上有优雅降级。**基础设施不缺，缺的是把它用到真实场景上。**

### 目标

**混合渲染**：光栅化出 GBuffer，再用光追做二次效果，逐项与其屏幕空间对照组对比（RT 阴影 vs Shadow Map、RTAO vs SSAO、RT 反射 vs SSR）——这个对比才是这个工程的核心价值。

### 算法清单

- [ ] **RT 硬阴影**：GBuffer 世界坐标出发投 shadow ray，与传统 Shadow Map 的 peter-panning / 走样对比。
- [ ] **RT 软阴影**：面光源上按锥角采样，每像素多根 ray + 时域累积。
- [ ] **RTAO**：半球余弦采样，对比 000/001 里的屏幕空间 AO。
- [ ] **RT 反射**：沿反射方向 trace，命中点做一次直接光照；与 SSR 的屏幕外信息缺失问题对比。
- [ ] **降噪（按需）**：先靠 TAA 时域累积（复用 004 的 history + motion vector），效果不够再考虑空间滤波（à-trous / SVGF），单独立项。



### 前置项（这才是真正的工作量）

- [ ] **多 mesh 场景的 AS 构建**：`Test_002` 只有一个三角形。Sponza 级场景需要按 mesh 批量建 BLAS、合并进 TLAS，并评估构建耗时与显存。
- [ ] **Hit shader 内的几何与材质寻址**：closesthit / rayQuery 命中后要拿到该三角形的法线、UV、材质 ID，需要顶点/索引缓冲以 SSBO 形式可索引，外加一张 instance → 几何偏移的映射表。**这里可能需要 bindless / 描述符数组支持，先确认 RHI 现状。**
- [ ] **GPU 侧材质表**：把 `AssetLoader` 的材质数据打成 SSBO，纹理走描述符数组。
- [ ] **后端能力分支**：GL 无光追，`--backend=gl` 时禁用 RT 分支并在 ImGui 上给出明确提示（沿用 `Test_002` 的 `GetCaps()` 降级写法）。
- [ ] **线程模式限制**：`Test_002` 的 README 已注明 Threaded 路径不支持 Compute/AS，本工程同样锁 Direct，或顺带评估解除该限制的成本。



### 触发条件

003 / 004 完成后启动。若只想尽快看到光追效果，可以先只做「RT 硬阴影」一项——它对材质寻址的依赖最弱（只需 hit / miss 布尔结果），能在几何与材质寻址那一堆前置项完工前先出图。

---



## 落地检查项

每个工程完成后统一补齐：

- [ ] 工程 README（参照 `001_Reflective_shadow_map/README.md`），含闭环图与运行参数。
- [ ] 加入 `TitusGLRenderer.sln`，并在根 `README.md` 的目录结构与示例工程表里登记。
- [ ] 双后端各出一组 `--screenshot-at` 对比图。
- [ ] 过 `Tools/check_no_backend_headers` 与 `Tools/check_deps_direction` 护栏。
- [ ] 期间发现的 RHI 缺口，按本仓惯例单开 `Docs/Todo/TODO_*.md`，不要堆在本文里。