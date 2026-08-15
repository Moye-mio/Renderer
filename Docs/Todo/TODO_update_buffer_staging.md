# 待办计划：GpuOnly Buffer 运行期 Staging 更新

## 背景

`VKDevice::UpdateBufferImpl`（`RendererVK/VKDevice.cpp`）目前只支持 **host-visible** 路径：`mappedPtr` 非空时直接 `memcpy`。

静态顶点/索引缓冲（`MemoryUsage::GpuOnly`）在创建时若带 `initialData`，会走 `UploadBufferViaStaging`（自动补 `TRANSFER_DST`）。但运行期再调 `UpdateBuffer` 会打错误日志并 return：

```text
UpdateBuffer on non host-visible buffer not implemented.
```

文档记载：`Docs/Architecture/22_VK_Step2_Buffer_Texture_Staging.md` §B3 / §G.1。

## 现状评估

| 路径 | 状态 |
|---|---|
| Create + `GpuOnly` + `initialData` | 已实现（staging + one-shot CB + `QueueWaitIdle`） |
| 运行期 `UpdateBuffer`（host-visible） | 已实现（持久 map + memcpy） |
| 运行期 `UpdateBuffer`（GpuOnly） | **未实现** |
| 运行期 `UpdateTexture`（GpuOnly image） | 已有 staging；可作 Buffer 路径参考 |

限制影响：业务若要对静态 VB/IB 做运行期局部改写，只能改用 `CpuToGpu`（带宽较差），或销毁重建。

## 待办项

- [ ] **实现 `UpdateBufferImpl` 的 GpuOnly 分支**：`mappedPtr == nullptr` 时复用 / 抽取与 `UploadBufferViaStaging` 相同的 staging copy（注意目标 buffer 创建时若未带 `initialData`，usage 可能缺少 `TRANSFER_DST`——需在 Create 时按 usage 预留，或 Update 前校验并文档约定业务必须声明 `TransferDst`）。
- [ ] **同步策略选型**（二选一或分阶段）：
  - **阶段 A（加载/偶发更新）**：继续 one-shot CB + `QueueWaitIdle`，与现有 Create 路径一致，实现简单。
  - **阶段 B（热路径）**：ring staging + 帧内 Fence / 延期销毁，避免每帧 `WaitIdle`；与 `Docs/Architecture/22_VK_Step2_Buffer_Texture_Staging.md` §D 代价说明对齐。
- [ ] **Destroy 安全**：staging 与目标 buffer 的生命周期须等 GPU 用完再释放（阶段 A 靠 WaitIdle；阶段 B 靠 in-flight 帧号 / Fence）。
- [ ] **（可选）GL 对齐**：确认 `GLDevice::UpdateBufferImpl` 对 GpuOnly 的语义，保持 RHI 层行为一致或在文档中写明后端差异。
- [ ] **触发条件**：业务需要运行期改写静态 VB/IB（蒙皮上传到 DEVICE_LOCAL、程序化网格局部更新等），或希望统一「Create / Update 都能 staging」API 时实施。

## 相关代码

- `VKDevice::UpdateBufferImpl`：`RendererVK/VKDevice.cpp`（非 host-visible 早退）
- `VKDevice::UploadBufferViaStaging`：同文件（Create 时 GpuOnly 初始上传）
- `VKDevice::CreateBufferImpl`：`GpuOnly + initialData` 自动 `| TRANSFER_DST`
- `VKBufferEntry::mappedPtr`：`RendererVK/VKDevice.h`
- 架构说明：`Docs/Architecture/22_VK_Step2_Buffer_Texture_Staging.md` §B2–B4、§G.1
- 已知限制索引：`Docs/Architecture/20_RendererVK.md`（UpdateBuffer 非 host-visible 未实现）

## 版本前提

- 无需新扩展；依赖现有 Transfer / CommandPool / graphics queue。
- 阶段 B 需与现有 frames-in-flight / pending destroy 机制协调，避免与 `imageAvailable` 信号混用出错。
