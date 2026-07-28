# 待办计划：支持每帧命令重录缓存（手动开启 + 自动识别）

## 背景

当前 `PassScheduler::DrawFrame`（`RendererCore/PassScheduler.cpp:66-71`）每帧都对每个 Pass 依次调用 `Update` + `Record`，**两个后端都从零重新录制整帧命令流**，无任何缓存复用：

- **VK 后端**：`VKDevice::BeginFrameImpl`（`RendererVK/VKDevice.cpp:1857` 附近）每帧 `primaryCmd->Reset()` 把 CommandBuffer 置回 INITIAL，再 `Begin()` 进入 RECORDING；CommandPool 建于 `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT`（`VKDevice.cpp:212`），即为「每帧单独 reset 重录」设计。
- **GL 后端**：`GLCommandList`（`RendererGL/GLCommandList.h`）内部维护 `std::vector<std::function<void()>> m_commands` 延迟队列，`Reset()` 每帧清空、`Record` 阶段重新 `Enqueue`、`Submit` 时 `Replay()` 回放。

对于**几何 / 绑定 / 视口均不变、仅 UBO 内容随帧变化**的静态场景，这种每帧全量重录属于重复劳动。目标是引入命令重录缓存：稳定的命令流录一次多帧复用，仅动态部分每帧更新。

## 现状约束（为什么现在不能直接缓存）

1. **DescriptorSet 每帧失效**：VK 端 `BeginFrameImpl` 每帧 `vkResetDescriptorPool`（`VKDevice.cpp:1859`）作废上一帧全部 DS，而 cmd buffer 内 `vkCmdBindDescriptorSets` 引用的正是这些 DS。只要 DS 每帧重分配，cmd buffer 就**必须**跟着重录，否则引用失效 DS。→ 缓存的前提是先做 **descriptor 稳定化**。
2. **GL 重录成本本就极低**：GL 是 immediate 转译，lambda 队列没有 driver 编译开销，缓存 `std::function` 队列收益有限，且会让动态数据失去时效。GL 端优先级低于 VK。
3. **帧间内容可能变化**：`IRenderPass::Update` 每帧可能改 transform / 增删物体 / 切材质，命令流并非总是稳定；需要一套判定「本帧命令流是否与缓存一致」的机制。

## 需求目标

支持两种触发模式：

### A. 手动开启（显式声明静态）
- 由业务 Pass / 上层显式标注「本 Pass 的命令流是静态的，可缓存复用」。
- 候选接口方向（待定）：
  - `IRenderPass` 增加能力查询，如 `virtual bool IsStaticRecord() const { return false; }`；或
  - 拆分录制职责：`RecordStatic()`（录一次进可复用 bundle）与 `RecordDynamic()`（每帧录，如 imgui / 动态物体）。
- `PassScheduler` 对声明为静态的 Pass 首帧录制并缓存，后续帧直接复用，跳过 `Record`。

### B. 自动识别（脏标记 / 指纹）
- 无需业务显式声明，由框架自动判断命令流是否变化：
  - 方案一（脏标记）：资源创建/销毁、绑定变更、视口变更、Pass 增删等操作置脏；无脏则复用缓存。
  - 方案二（指纹/哈希）：对一帧录制产生的命令序列（或其关键参数）做哈希，与上一帧比对，一致则可复用。
- 自动识别应可被手动模式覆盖（手动优先）。

## 实施路线（分阶段）

- [ ] **阶段 0（前置，必做）：VK descriptor 稳定化**
  - 把每帧 reset 的 per-frame DescriptorPool 改为「持久 DS + dynamic UBO offset」（UBO 用 `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`，每帧只换 offset，DS 本身跨帧有效）。
  - 这是任何 cmd 复用的先决条件，本身也能显著降低每帧 descriptor 分配开销。
- [ ] **阶段 1：接口扩展**
  - 在 `RenderCommandList` 增加「录制到可复用 bundle / 执行 bundle」的能力抽象。
  - 在 `IRenderPass` 引入静态/动态录制的职责划分或能力查询。
- [ ] **阶段 2：VK 后端缓存实现（手动模式）**
  - 用 **secondary command buffer** 把静态场景绘制预录成 bundle，primary cmd 每帧仅 `vkCmdExecuteCommands` 调用。
  - `PassScheduler` 首帧录制并缓存，后续帧复用。
- [ ] **阶段 3：自动识别（脏标记）**
  - 在 `IGDevice` / 资源管理层引入脏标记，`PassScheduler` 据此决定复用或重录。
- [ ] **阶段 4（可选）：指纹校验兜底**
  - 对命令序列做哈希比对，作为自动识别的兜底或调试校验手段。
- [ ] **阶段 5（可选）：GL 后端支持**
  - 视收益决定是否缓存 lambda 队列；GL 优先级最低，多数情况保持每帧重录即可。

## 缓存失效条件（无论手动 / 自动都必须处理）

- Pass 增删（`PassScheduler::AddPass` / `RemoveAllPasses`）。
- 绑定资源（Pipeline / VertexBuffer / IndexBuffer / ResourceSet / 纹理）变更。
- 视口 / 裁剪 / RenderTarget 尺寸变化（含窗口 resize、swapchain 重建）。
- swapchain out of date / 重建（`AcquireCommandList` 返回空的路径，`PassScheduler.cpp:56`）。
- 手动模式下业务显式声明缓存失效。

## 注意事项

- **动态部分不能进静态 bundle**：如 imgui overlay（`PassScheduler.cpp:87`，且 VK 端 imgui 录制被挪到 `SubmitImpl` 内 `End()` 之前）、动态物体、每帧变化的 push constants。
- **收益评估先行**：当前示例级规模（三角形 / Deferred / RayQuery），CPU 命令录制大概率**不是瓶颈**，真正开销在 GPU 执行、descriptor 分配、GL driver 调用。实施前应先 profile 确认录制开销可观，再推进阶段 2 及以后。
- 建议优先落地**阶段 0**（descriptor 稳定化）——即使暂不做 cmd 缓存，它本身也是收益明确、风险可控的独立优化。

## 触发条件

- 出现「静态场景 + draw call 数量大（上千级）」且 profile 确认 CPU 命令录制成为瓶颈时实施。
- 否则维持现状（每帧重录），不引入缓存复杂度。

## 相关定义 / 代码位置

- 帧调度：`RendererCore/PassScheduler.cpp:47-92`（`DrawFrame`）
- Pass 接口：`RendererCore/IRenderPass.h`（`Update` / `Record`）
- 命令录制接口：`RendererCore/RenderCommandList.h`
- VK 每帧重录：`RendererVK/VKDevice.cpp:1824-1871`（`BeginFrameImpl`）、`RendererVK/VKCommandList.h`
- VK descriptor 每帧整池 reset：`RendererVK/VKDevice.cpp:2054` 附近
- GL 延迟队列：`RendererGL/GLCommandList.h`（`Reset` / `Replay` / `m_commands`）
