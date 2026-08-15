# RendererVK 详解（Vulkan 后端）

> 逐文件 → class/struct → 职责 + 热点函数下钻。
> 命名空间：`TitusVkGraphics`；对外符号经 `RENDERER_VK_DLLEXPORTS`（`RENDERER_VK_EXPORTS.h`）导出。
> 定位：用 Vulkan 实现 `RendererCore` 的 `GDevice` 契约（`*Impl()` 钩子），是 `--backend=vk` 的落地。

---

## 0. 定位：单一"设备实现"职责

> **历史说明**：早期 `RendererVK` 曾与 `RendererGL` 对称地内含一套独立渲染引擎——`VkApp`/`VkInterface`/`VkPassScheduler`/`VkResourceManager`/`IVkRenderPass`，以及 `VkShaderModuleWrapper`/`VkGraphicsPipeline` 等封装。随着业务 Pass 全部迁移到 `RendererCore::IRenderPass` 统一基类、调度收敛到 `RendererCore::PassScheduler`，这套旧骨架已**整体清退删除**。
>
> 本库现在只承担唯一职责：**用 Vulkan 落地 `RendererCore` 的 `GDevice` 契约**。`VKDevice` 内部直接创建 `VkPipeline`/`VkShaderModule`，不再经旧的 `VkGraphicsPipeline`/`VkShaderModuleWrapper` 封装。

---

## 1. 文件全景

| 分组 | 文件 | 一句话职责 |
|---|---|---|
| **导出/全局** | `RENDERER_VK_EXPORTS.h` | DLL 导出宏 |
| | `Common.{h,cpp}` | 全局配置、`VK_CHECK`、队列族/交换链支持结构 |
| **底层封装** | `VkWindow.{h,cpp}` | GLFW 窗口 + `VkSurfaceKHR` |
| | `VkContext.{h,cpp}` | Instance/Device/Queue + 光追能力探测 |
| | `VkSwapchainWrapper.{h,cpp}` | 交换链 + Depth + 默认 RenderPass + Framebuffer |
| | `VkCommandBufferWrapper.{h,cpp}` | Primary/Secondary CommandBuffer 封装 |
| **设备实现** | `VKDevice.{h,cpp}` | `GThreadableDevice` 的 Vulkan 实现（设备主体） |
| | `VKCommandList.{h,cpp}` | `RenderCommandList` 的 Vulkan 实现 |
| | `VKTranslate.{h,cpp}` | Core 枚举 → Vulkan 枚举翻译 |
| | `VKShaderCompiler.{h,cpp}` | GLSL/HLSL → SPIR-V 编译（+ 反射） |
| | `VKDeviceFactory.cpp` | 桥接创建入口（由 `RendererInterface::GDeviceFactory` 调用）|

> 就这些编译单元。Pass 调度 / 资源 / 相机 / ImGui 等后端无关能力由 `RendererCore` + `RendererInterface` 承担（imgui 源码由 `RendererInterface.vcxproj` 统一编译，含 `imgui_impl_vulkan`）。

---

## 2. 全局配置 · `Common.{h,cpp}`

- `struct QueueFamilyIndices`（`Common.h`）：`graphicsFamily/presentFamily/computeFamily/transferFamily`（默认 `UINT32_MAX`）；`IsComplete()` 要求 graphics+present 均有效。
- `struct SwapchainSupportDetails`：`capabilities`/`formats`/`presentModes`。
- `VK_CHECK` 宏、验证层开关等全局配置。

> 排序事件枚举 `ERenderPassEvent` 现由 `RendererCore/IRenderPass.h` 的 `TitusRHI::ERenderPassEvent` 统一持有（供 `PassScheduler` 排序），不再在 VK 侧另存一份。

---

## 3. 上下文 · `VkContext.{h,cpp}` ★

`class VkContext`（`VkContext.h:19`）——显式管理所有 Vulkan 核心对象与队列族，是设备层能力查询中心。
- 关键成员：`m_instance`、`m_debugMessenger`、`m_surface`、`m_physicalDevice`、`m_device`、`m_graphicsQueue`、`m_presentQueue`、`m_queueFamilyIndices`；常量 `m_validationLayers`、`m_deviceExtensions`。
- `Init(window)`（`VkContext.cpp:41`）：`CreateInstance → SetupDebugMessenger → window.CreateSurface → PickPhysicalDevice → CreateLogicalDevice`。
- `CreateInstance()`（`VkContext.cpp:77`）：校验层可用性 → 填 `VkApplicationInfo` → 收集 GLFW 扩展 →（Debug 时挂 messenger）→ `vkCreateInstance`。

### 光追能力（受 `RENDERER_ENABLE_RAY_TRACING` 宏控制）
- getter 组（`VkContext.h:51-80`）：`SupportsRayTracing/SupportsRayQuery/GetAccelStructProps/SupportsRayTracingPipeline/GetRTPipelineProps/RT()`；`#else` 分支（`:82-84`）提供恒 false 桩。
- 内嵌 `struct RayTracingFunctions`（`VkContext.h:67`）：9 个 RT/AS/BDA/SBT 扩展函数指针。
- 私有 `CheckRayTracingSupport/CheckRayTracingPipelineSupport/LoadRayTracingFunctions`（`:101-108`）；成员 `m_supportsRayTracing/m_accelStructProps/m_rtFunctions` 等（`:128-155`）。

> ⚠️ 这些**受宏控制的成员改变了类布局**——正是 [`2026-07-16_vk_odr_crash.md`](../Bug/2026-07-16_vk_odr_crash.md) 记录的 ODR/堆破坏根因链的上游（`VKDevice` 也含此类宏成员）。详见 `99_Pitfalls.md`。

---

## 4. 交换链 · `VkSwapchainWrapper.{h,cpp}`

封装：交换链创建（选 surface format / present mode / extent）、Depth 附件、默认 RenderPass、Framebuffer 集合，以及窗口 resize 时的**重建**（out-of-date/suboptimal 时销毁旧链重建）。多帧 In-Flight 的图像数由此管理。

---

## 5. 命令缓冲封装 · `VkCommandBufferWrapper.{h,cpp}`

- `VkCommandBufferWrapper.{h,cpp}`：Primary/Secondary `VkCommandBuffer` 的分配、Begin/End、录制辅助。被 `VKDevice`（每帧 primary cmd）复用。

> Pipeline 与 Shader 不再有独立封装类：`VKDevice::CreatePipelineImpl` 直接 `vkCreateGraphicsPipelines/vkCreateComputePipelines` 固化 PSO；`CreateShaderImpl` 经 `VKShaderCompiler` 产出 SPIR-V 后 `vkCreateShaderModule`（连同 reflection）。

---

## 6. 设备实现 · `VKDevice.{h,cpp}` ★

`class VKDevice : public ::TitusRHI::GThreadableDevice`（`VKDevice.h:155`）——Vulkan 版 `GDevice`，实现全部 `*Impl()` 钩子。
- 内部维护 6 张 `id → 原生对象` 映射表（`mBuffers/mTextures/mSamplers/mShaders/mPipelines/mRenderTargets`，`VKDevice.h:382-387`），持有 `VkContext`/`VkSwapchainWrapper`/`VkCommandPool`/每帧 `VKFrameSync` 数组 `mFrames`/当前帧 `VKCommandList` 等。
- **自管窗口**：`OnInitBackend` **无视上层传入的 `IWindow*`，改为自建 `VkWindow mInternalWindow`**（`VKDevice.h:360-361`；见 `VKDevice.cpp:46-73` 的注释，为规避 type-confusion）。主循环通过 `IsWindowClosed()`/`GetWindowNativeHandle()`（`VKDevice.h:170-173`）向 Device 问询窗口状态。
- **受 `RENDERER_ENABLE_RAY_TRACING` 宏控制的成员**：`std::unordered_map<uint64_t, VKAccelStructEntry> mAccelStructs`（`VKDevice.h:388-391`），其后还有 `mImGuiCallback/mImGuiUserData` 等——该宏改变 `sizeof`/成员偏移。

### 资源条目 struct（`VKDevice.h`）
| struct | 行号 | 关键字段 |
|---|---|---|
| `VKBufferEntry` | `:34` | `VkBuffer buffer`、`VkDeviceMemory memory`、`size`、`usage/memProps`、`void* mappedPtr`（host-visible 时持久映射） |
| `VKTextureEntry` | `:44` | `VkImage image`、`VkImageView defaultView`、`format`、`width/height`、`mipLevels/arrayLayers`、`VkImageLayout currentLayout`（随命令流更新） |
| `VKSamplerEntry` | `:57` | `VkSampler sampler` |
| `VKShaderEntry` | `:62` | `VkShaderModule module`、`stage`、`entry`、`ReflectionInfo reflection` |
| `VKPipelineEntry` | `:70` | `VkPipeline`、`VkPipelineLayout`、`setLayouts[]`、`isCompute/bindPoint`、`compatRenderPass/ownsCompatRenderPass`（见 §6.5）、RT 宏下 `sbtBuffer` + 四个 SBT region |
| `VKRenderTargetEntry` | `:96` | `VkFramebuffer`、`VkRenderPass`、`extent`、`colorAttachments`、`depthStencilAttachment` |
| `VKAccelStructEntry`（RT 宏） | `:115` | `VkAccelerationStructureKHR as`、backing `buffer/memory`、`deviceAddress`、TLAS 专用 `instanceBuffer`、动态更新的 `updateScratch` 等 |
| `VKFrameSync` | `:142` | `imageAvailable/renderFinished` semaphore、`inFlightFence`、`primaryCmd`、每帧 `VkDescriptorPool descriptorPool` |

### 钩子实现要点（`VKDevice.cpp`，带行号）
| 钩子 | 行号 | 职责 |
|---|---|---|
| `OnInitBackend` | `:46` | 自建 `VkWindow` → `VkContext::Init`（Instance/Device/Queue）→ `CreateCommandPool` → `FillCaps` 填 `mCaps` |
| `OnInitSwapchain` | `:75` | 建 `VkSwapchainWrapper`（交换链/Depth/默认 RP/FBO）→ `CreateSyncObjects` → `CreateDescriptorPools` → 建 `VKCommandList` |
| `CreateBufferImpl` | `:662` | 建 `VkBuffer` + `AllocateBufferMemory`；GpuOnly+initialData 自动补 `TRANSFER_DST` 走 staging，host-visible 直接 `memcpy` 到 `mappedPtr` |
| `CreateTextureImpl` | `:734` | 建 `VkImage` + `VkImageView` + 分配内存，`mipLevels==0` 时自动计算层数 |
| `CreateSamplerImpl` / `CreateShaderImpl` | — | 建 `VkSampler` / 经 `VKShaderCompiler` 从 SPIR-V 建 `VkShaderModule`（连同 reflection） |
| `CreatePipelineImpl`(Graphics) | `:971` | 建 DescriptorSetLayout → 选/建**兼容 RenderPass**（见 §6.5）→ 直接 `vkCreateGraphicsPipelines` 固化 PSO |
| `CreatePipelineImpl`(Compute) | `:1215` | 校验 CS → 建 compute `VkPipeline`（`isCompute=true`/`bindPoint=COMPUTE`） |
| `CreatePipelineImpl`(RayTracing，RT 宏) | `:1321` | 建 RT `VkPipeline` + 分配 SBT buffer 与四个 region |
| `CreateRenderTargetImpl` | — | 建 `VkFramebuffer` + 对应 `VkRenderPass` |
| `CreateAccelerationStructureImpl`(RT 宏) | `:369` | 建 BLAS/TLAS（backing buffer + scratch + TLAS instance buffer） |
| `UpdateBufferImpl` | `:719` | host-visible 直接 `memcpy`；非 host-visible 目前 **not implemented**（已知限制） |
| `UpdateTextureImpl` | — | 经 `UploadImageViaStaging` 拷 subresource |
| `BeginFrameImpl` | `:1824` | `vkWaitForFences` → `vkAcquireNextImageKHR`（out-of-date 时 `Recreate`）→ resetFences → `primaryCmd->Reset` → **整池 reset 本帧 DescriptorPool** → Begin |
| `AcquireCommandListImpl` | `:1873` | 返回 `mCommandList.get()`（已绑定本帧 primary cmdbuf） |
| `SubmitImpl` | `:1878` | `RecordImGuiOverlayInPrimaryCmd()`（`:1886`）→ `primaryCmd->End()`（`:1889`）→ `vkQueueSubmit`（图形队列） |
| `PresentImpl` | `:1911` | `vkQueuePresentKHR`；out-of-date/suboptimal/resized 时 `Recreate` |
| `OnWaitIdleImpl` | `:191` | `vkDeviceWaitIdle` |
| `OnWindowResizedImpl` | `:196` | 置 resize 标志，触发交换链重建 |
| `OnShutdownSwapchain` / `OnShutdownBackend` | — | 逆序销毁 DescriptorPool/Sync/Swapchain 与 Context/CommandPool |

> `SubmitImpl` 内在 `End()` 之前录制 imgui（`RecordImGuiOverlayInPrimaryCmd`），public `RenderImGuiOverlay()`（`:1971`）已退化为 no-op；这是 `PassScheduler::DrawFrame` 中"Submit 后 `RenderImGuiOverlay` 无重复执行"的原因（见 `90_Flows.md` / `99_Pitfalls.md §2`）。

---

## 6.5 VK 特有机制（"显式性"落地点）★

以下机制是 Vulkan 后端相较 GL 的核心差异，也是最易踩坑处：

### 每帧 DescriptorPool + 帧内 DS 分配
- 每个 `VKFrameSync` 持一个 `VkDescriptorPool`（`VKDevice.h:149`），`CreateDescriptorPools/DestroyDescriptorPools`（`VKDevice.h:287-288`）随交换链生命周期创建/销毁。
- `BeginFrameImpl` 对本帧 DescriptorPool **整池 `vkResetDescriptorPool`**，回收上一轮 DS。
- `VKCommandList::BindResourceSet` 经 `VKDevice::AllocateDescriptorSet(layout)`（`VKDevice.h:225`）从本帧池分配 DS；DS 生命周期仅覆盖"本帧 Submit 完成 → 下轮同帧 BeginFrame"。

### per-image `renderFinished` semaphore（反 VUID 复用）
- 除每帧 `VKFrameSync::renderFinished` 外，额外维护 **per-swapchain-image** 的 `mImageRenderFinishedSemaphores`（`VKDevice.h:372`）。
- 目的：当 `framesInFlight < imageCount` 时避免同一 semaphore 被两帧复用，规避 `VUID-vkQueueSubmit-pSignalSemaphores-00067`。这是非平凡的正确性设计。

### Staging 上传基础设施
- 通用 one-shot 命令缓冲：`BeginOneTimeCommands/EndOneTimeCommands`（`VKDevice.h:306-307`），走 graphicsQueue + `vkQueueWaitIdle`，与 frame-in-flight 无关。
- `UploadBufferViaStaging`（`VKDevice.h:313`）：建 host-visible staging → `memcpy` → `vkCmdCopyBuffer` → 释放。
- `UploadImageViaStaging`（`VKDevice.h:324`）：staging → `UNDEFINED→TRANSFER_DST` → `vkCmdCopyBufferToImage` → `TRANSFER_DST→finalLayout`。调用方需保证 image usage 含 `TRANSFER_DST`。
- `TransitionImageLayoutImmediate`（`VKDevice.h:290`）：immediate one-shot layout 转换。

### 内存分配与映射
- `AllocateBufferMemory`（`VKDevice.h:338`）按 `typeFilter + 属性`挑 memoryType。
- host-visible buffer 建时持久映射到 `mappedPtr`（`VKBufferEntry`，`VKDevice.h:41`），`UpdateBufferImpl` 直接 `memcpy`；GpuOnly 走 staging。**非 host-visible 的 `UpdateBufferImpl` 尚未实现**（`VKDevice.cpp:725` 报错），是当前已知限制。

### 兼容 RenderPass（graphics pipeline 特有约束）
- VK 要求 pipeline 烘焙时的 RenderPass 与录制期 `BeginRenderPass` 的 RP **兼容**。
- 若 pipeline 的 `RenderTargetLayout` 与 swapchain 默认 RP 不兼容，`CreateCompatibleRenderPass`（`VKDevice.h:301`）建一个仅用于 compatibility 的临时 RP，存入 `VKPipelineEntry::compatRenderPass`（`ownsCompatRenderPass` 标记所有权），销毁 pipeline 时一并释放。

### Resize 三点重建
交换链 `Recreate` 的触发点分布在三处：`OnWindowResizedImpl`（`:196`，置标志）、`BeginFrameImpl`（acquire 返回 out-of-date）、`PresentImpl`（present 返回 out-of-date/suboptimal 或 resized 标志）。

---

## 7. 命令录制 · `VKCommandList.{h,cpp}`

`RenderCommandList` 的 Vulkan 实现：内部持一个 `VkCommandBuffer`（`mCmd`，每帧由 `VKDevice::Reset` 提供），逐条翻译：

| 命令 | 翻译 |
|---|---|
| `BeginRenderPass` / `EndRenderPass` | `vkCmdBeginRenderPass`（按 LoadOp/StoreOp）/ `vkCmdEndRenderPass` |
| `SetViewport` / `SetScissor` | `vkCmdSetViewport` / `vkCmdSetScissor` |
| `BindPipeline` | `vkCmdBindPipeline`（记录 `mCurrentLayout/mCurrentBindPoint/mCurrentPipelineEntry`） |
| `BindVertexBuffer` / `BindIndexBuffer` | `vkCmdBindVertexBuffers` / `vkCmdBindIndexBuffer` |
| `BindResourceSet` | 见下方"GL 风格增量绑定兼容层" → `vkCmdBindDescriptorSets` |
| `PushConstants` | `vkCmdPushConstants` |
| `Draw` / `DrawIndexed` | `vkCmdDraw` / `vkCmdDrawIndexed` |
| `Dispatch` | `vkCmdDispatch`（按 `mCurrentBindPoint=COMPUTE`） |
| `PipelineBarrier` | `vkCmdPipelineBarrier` |
| `TraceRays`（RT 宏） | `vkCmdTraceRaysKHR`（用 pipeline 的四个 SBT region） |
| `BuildAccelerationStructure`（RT 宏） | `vkCmdBuildAccelerationStructuresKHR`（支持 UPDATE 模式 refit） |

> **GL 风格增量绑定兼容层**（`VKCommandList.h:102-116`）：业务侧假设 GL 语义——先 `BindResourceSet(0, {UBO@0})`，循环内 `BindResourceSet(0, {Diffuse@1})` 只写单个 binding。GL 下 binding 是全局状态机互不干扰；VK 端为模拟此行为，维护 per-set 完整 `ResourceSetDesc` 缓存（`mSetStateCache`，最多 `kMaxCachedSets=8` 个 set），每次绑定把新 binding **合并**进完整状态后分配**新的** DS 并写入完整状态（既不违反"不得更新已绑定 DS"的 VK 规范，又保留增量语义）。`BindPipeline` 切换 pipeline 时清空缓存。此外 `BeginRenderPass`/`EndRenderPass` 会同步 color attachment 的 `currentLayout` 至 `SHADER_READ_ONLY_OPTIMAL`，供后续 Pass 采样。

---

## 8. 枚举翻译 · `VKTranslate.{h,cpp}`

把 Core 枚举映射为 Vulkan 枚举：`Format→VkFormat`、`PrimitiveTopology→VkPrimitiveTopology`、`BlendFactor/BlendOp→Vk*`、`CompareOp→VkCompareOp`、`LoadOp/StoreOp→VkAttachmentLoadOp/StoreOp`、`ShaderStage→VkShaderStageFlags`、`BufferUsage/TextureUsage→VkBufferUsageFlags/VkImageUsageFlags`、`AddressMode/FilterMode→Vk*` 等。是"抽象枚举 ↔ Vulkan"的唯一翻译点。

---

## 9. 着色器编译 · `VKShaderCompiler.{h,cpp}`

Vulkan 只吃 SPIR-V。本模块把 GLSL/HLSL 源码编译为 SPIR-V 字节码（配合示例的 `Shader/CompileShaders.bat` 用 `glslc` 预编译），并可产出反射信息喂给 `ShaderDesc.reflection`。

---

## 10. 与 OpenGL 后端的关键差异

1. 显式管理上下文（Instance/Device/Queue 手工创建销毁）；
2. 显式交换链（Acquire/Present 取代 `glfwSwapBuffers`）；
3. 显式同步（Fence 管 CPU-GPU，Semaphore 管 GPU-GPU）；
4. Pipeline 固化（状态机 → immutable Pipeline）；
5. Shader 预编译（GLSL → SPIR-V）；
6. 命令缓冲显式化（`VkCommandBuffer`）。

> 帧绘制流程（`BeginFrame → AcquireCommandList → Pass::Record → Submit → Present`）由 `RendererCore::PassScheduler::DrawFrame` 统一驱动，GL/VK 共用同一份调度逻辑，见 `90_Flows.md`。
