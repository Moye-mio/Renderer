# RendererCore 详解（抽象层）

> 第 2/3 遍：逐文件 → class/struct → 职责 + 热点函数下钻（带代码行号引用）。
> 命名空间：`TitusRHI`。本层**严禁** include 任何后端 SDK 头，是整套架构的"契约中心"。
> 依赖阅读：先看 `00_Overview.md` 的继承链与设计决策。

---

## 0. 文件全景

| 分组 | 文件 | 一句话职责 |
|---|---|---|
| **句柄/枚举/描述** | `GHandle.h` | 不透明、类型安全的资源句柄 |
| | `GEnums.h` | 后端无关枚举（格式/拓扑/混合/阶段/用途…） |
| | `GDescs.h` | 资源与管线状态描述结构体 |
| **设备契约与基类** | `IGDevice.h` | 对外最小设备接口（纯虚） |
| | `GDevice.{h,cpp}` | 后端无关基类：模板方法 + 句柄分配 + 延迟销毁 |
| | `GThreadableDevice.{h,cpp}` | 叠加渲染线程归属能力 |
| | `GDeviceHeadless.h` | 空后端（headless / 测试） |
| **命令录制** | `RenderCommandList.h` | 后端无关命令录制接口 |
| **线程模型** | `GThreadingMode.h` | 三种线程模式枚举 |
| | `GDeviceMainThread.{h,cpp}` | Threaded 模式主线程门面 |
| | `GDeviceWorker.{h,cpp}` | Threaded 模式工作线程 |
| | `CommandRingBuffer.{h,cpp}` | SPSC 字节流命令队列 |
| **调度与 Pass** | `PassScheduler.{h,cpp}` | 后端无关帧调度器 |
| | `IRenderPass.h` | 业务 Pass 基类 + `ERenderPassEvent` |
| | `IWindow.h` | 窗口抽象 |
| **内部支撑** | `HandleAllocator.h` | 句柄 id 分配器 |
| | `GContextData.h` | 设备上下文数据 |
| | `GResources.h` | 后端无关资源元数据包装（`RHIBuffer`/`RHITexture`/`RHIShader`） |
| | `GStateCache.h` | `SamplerCache`/`PipelineCache` 去重缓存 |
| **工厂** | `GDeviceFactory.{h,cpp}` | 按后端创建设备（Core 侧工厂） |
| **着色器/材质** | `ShaderReflection.h`/`ShaderReflector.{h,cpp}` | 反射信息与反射器 |
| | `ShaderAsset.{h,cpp}` | 着色器资产（编译产物 + 反射） |
| | `ShaderParameterSet.h` | 着色器参数集 |
| | `Material.{h,cpp}` / `MaterialInstance.h` | 材质与材质实例 |
| **网格/模型/上传** | `GpuMesh.h` / `GpuModel.{h,cpp}` | GPU 网格/模型 |
| | `AssetGpuUploader.{h,cpp}` | 资产 → GPU 上传器 |
| **光追** | `RayTracingManager.{h,cpp}` | 加速结构管理 |
| | `RendererCore.cpp` | 模块聚合/测试入口 |

---

## 1. 句柄系统 · `GHandle.h`

- `struct GHandle<Tag>`（`GHandle.h:16`）：内部仅 `uint64_t id = 0`（POD），`Tag` 提供编译期类型安全；`IsValid()`（`:27`）判定 `id != 0`；`operator==/!=`（`:29-30`）仅允许同 Tag 比较。
- Tag 空结构（`:36-63`）：`BufferTag / TextureTag / SamplerTag / ShaderTag / PipelineTag / RenderTargetTag / AccelerationStructureTag`。
- 别名（`:68-75`）：`BufferHandle / TextureHandle / SamplerHandle / ShaderHandle / PipelineHandle / RenderTargetHandle / AccelerationStructureHandle`。

> **设计点**：句柄是"跨越抽象墙"的唯一凭证——上层拿到的永远是 id，不同后端各自维护 `id → 原生对象` 的映射表。

---

## 2. 枚举 · `GEnums.h`

集中定义后端无关枚举，不绑定任何后端具体值（由各后端 `*Translate` 负责翻译）：

| 枚举 | 行号 | 说明 |
|---|---|---|
| `GBackend` | `:15` | `Unknown/OpenGL/Vulkan/Null` |
| `Format` | `:27` | 像素/顶点/索引格式 |
| `PrimitiveTopology` | `:65` | 图元拓扑 |
| `IndexType` | `:78` | `UInt16/UInt32` |
| `CullMode`/`FrontFace`/`PolygonMode` | `:87/95/101` | 光栅化状态 |
| `CompareOp` | `:111` | 深度/模板比较 |
| `BlendFactor`/`BlendOp` | `:126/142` | 混合 |
| `LoadOp`/`StoreOp` | `:154/161` | 附件加载/存储 |
| `ShaderStage` | `:170` | 阶段位标志（含光追 RayGen/Miss/ClosestHit） |
| `BufferUsage` | `:201` | Buffer 用途位标志（含 ShaderDeviceAddress/AS） |
| `TextureUsage` | `:236` | 纹理用途位标志 |
| `MemoryUsage`/`TextureType`/`FilterMode`/`MipmapMode`/`AddressMode` | `:265/276/285/291/297` | 内存/纹理/采样 |

位标志枚举提供 `operator|/&` 与 `HasFlag`（如 `:188/193/218/223/228`）。

---

## 3. 描述结构体 · `GDescs.h`

后端无关的"资源与管线状态描述"，是 `IGDevice::Create*` 与 `RenderCommandList` 的参数载体。核心结构：

| 结构体 | 行号 | 关键字段 |
|---|---|---|
| `Viewport`/`Rect2D`/`Extent2D`/`ClearValue` | `:24/34/42/52` | 视口/矩形/尺寸/清屏值 |
| `BufferDesc` | `:64` | `size, usage, memory, initialData, debugName` |
| `TextureDesc` | `:77` | `type,format,width,height,depth,mipLevels,arrayLayers,samples,usage` |
| `TextureUploadDesc` | `:92` | 单次上传：`data,bytes,mipLevel,arrayLayer,width/height/depth,offset*` |
| `SamplerDesc` | `:109` | 过滤/寻址/lod/anisotropy/compareOp/borderColor（可 memcmp 哈希做去重） |
| `ResourceBinding`/`PushConstantRange`/`ReflectionInfo` | `:143/153/164` | 反射绑定项/push constant/反射集合 |
| `ShaderDesc` | `:173` | `stage,code,bytes,entryPoint,reflection,debugName` |
| `VertexAttribute`/`VertexBinding`/`VertexLayout` | `:192/…` | 顶点输入布局 |
| 管线状态 `RasterizerState`/`DepthStencilState`/`BlendState` | — | 光栅化/深度模板/混合状态 |
| `GraphicsPipelineDesc`/`ComputePipelineDesc`/`RayTracingPipelineDesc` | — | 三类管线描述 |
| `RenderPassBeginInfo`/`RenderTargetDesc` | — | RP 开始信息/渲染目标 |
| `GDeviceDesc`/`GCaps` | — | 设备创建参数/能力查询 |
| 光追：`BLASGeometryDesc`/`TLASInstanceDesc`/`AccelerationStructureDesc`/`ASBuildFlags` | — | 加速结构描述 |
| `PipelineBarrierDesc` | — | 流水线屏障 |

> `ResourceBinding.setIndex` 语义对齐 Vulkan descriptor set；`PushConstantRange` 额外带 `glName` 供 GL 回退成 `glUniform*`。

---

## 4. 设备契约 · `IGDevice.h`

`class IGDevice`（`IGDevice.h:24`）——对外可见的最小接口，方法分组：

- **生命周期**：`Init`（`:33`）/`Shutdown`（`:36`）/`WaitIdle`（`:39`）/`OnWindowResized`（`:42`）
- **资源创建/销毁**：`CreateBuffer/Texture/Sampler/Shader/RenderTarget`（`:47-58`）；`CreatePipeline` 三重载（Graphics/Compute/RayTracing，`:51-57`）；`CreateAccelerationStructure`（`:61`）；成组 `Destroy`（`:63-69`）
- **数据上传**：`UpdateBuffer`（`:75`）/`UpdateTexture`（`:81`）
- **帧控制**：`BeginFrame`（`:89`）/`AcquireCommandList`（`:93`）/`Submit`（`:96`）/`Present`（`:99`）/`GetCurrentFrameIndex`（`:102`）
- **能力查询**：`GetBackend`（`:107`）/`GetCaps`（`:108`）/`IsWindowClosed`（`:113`，默认 false）/`GetWindowNativeHandle`（`:119`，默认 nullptr，仅 VK 自管窗口时供 Interface 内部输入模块用）
- **ImGui Hook**：`SetImGuiOverlayCallback`（`:131`）/`RenderImGuiOverlay`（`:132`），默认空实现——由 `PassScheduler` 在录制后、Submit 后调用，后端只负责"在正确时机以正确状态调回调"。

> 关键约束（`:87-92` 注释）：多帧 In-Flight 的 Fence/Semaphore **完全封装在后端内部不外泄**；`AcquireCommandList` 返回的指针由设备管理，调用方不得 delete，Submit 后即失效。

---

## 5. 后端无关基类 · `GDevice.{h,cpp}` ★核心

`class GDevice : public IGDevice`（`GDevice.h:66`）用**模板方法**把通用逻辑下沉，子类只实现 `*Impl()` 钩子。

### 5.1 模板方法：`Init`（`GDevice.h:84`）
固定执行顺序（`.h:76-83` 注释）：① 参数校验 → ② `OnInitBackend`（子类建 Instance/Device/Context）→ ③ `OnInitSwapchain`（子类建 Swapchain/默认 FBO）→ ④ 初始化句柄表/缓存 → ⑤ `PostInitBackend(onRenderThread)`（`GThreadableDevice` 增强登记线程归属）。任意阶段失败回滚并返回 false。

### 5.2 资源创建：`CreateBuffer` 等（`GDevice.h:101-111`）
基类实现（`.cpp`）统一做：**参数校验 → `mHandleAllocator` 分配 id → 调用纯虚 `CreateBufferImpl(id, desc)`（`.h:177`）→ 成功则在 `mBufferRegistry` 登记后端无关元数据（`RHIBuffer`）→ 返回 `BufferHandle(id)`**。子类只关心"怎么建"，不碰 id 分配。
- Compute/RayTracing 管线 `*Impl` 提供默认返回 `false`（`.h:185/191`），未接入的后端无需被迫实现，调用者以 `IsValid()` 判定。

### 5.3 延迟销毁（Frames-in-Flight 安全）
- `Destroy(handle)` → `EnqueueDestroy(kind, id)`（`.h:238`）压入 `mPendingDestroyQueue`，记录入队帧号；
- `Present()` 内调 `ProcessPendingDestroysIfReady()`（`.h:239`）：对 `submitFrame + framesInFlight <= mCurrentFrameIndex` 的条目才调 `Delete*Impl(id)` 真正释放；
- `Shutdown()` 前 `FlushAllPendingDestroys()`（`.h:240`）强制清空。

### 5.4 帧控制与状态缓存
- `BeginFrame/AcquireCommandList/Submit/Present`（`.h:134-137`）默认转发到对应 `*Impl()`（`.h:225-228`）。
- 资源查询 `FindBuffer/FindTexture/FindShader`（`.h:153-155`）供 Material/Pass 反查后端无关元数据。
- 关键成员（`.h:254-274`）：`mHandleAllocator`、`mGContextData`、`mPendingDestroyQueue`、`mCurrentFrameIndex`、`mSubmitFrameCount`、三张 `*Registry`、`mSamplerCache`/`mPipelineCache`、`mWindow`、`mDesc`、`mCaps`。

### 5.5 子类钩子清单（`protected`）
必须实现：`OnInitBackend`/`OnInitSwapchain`/`OnShutdownSwapchain`/`OnShutdownBackend`（`.h:162-165`）、`OnWaitIdleImpl`（`.h:171`）、各 `Create*Impl`/`Delete*Impl`/`Update*Impl`、`BeginFrameImpl`/`AcquireCommandListImpl`/`SubmitImpl`/`PresentImpl`。

---

## 6. 线程感知 · `GThreadableDevice.{h,cpp}`

`class GThreadableDevice : public GDevice`（`GThreadableDevice.h:22`）：
- `mOwnerThread`（`std::atomic<std::thread::id>`，`:44`）记录渲染线程归属；
- `AcquireThreadOwnership`/`ReleaseThreadOwnership`（`:30-31`）：Worker 线程循环开始/结束时接管/让渡；
- `PostInitBackend`（`:38`）重写：Init 末尾按参数决定是否把当前线程登记为 owner；
- `AssertOnRenderThread`（`:41`）重写为真正的线程比对（开发期校验）。

GL/VK 后端均继承本类（`GLDevice.h:89` / `VKDevice.h:155`）。

---

## 7. 命令录制 · `RenderCommandList.h`

`class RenderCommandList`（`RenderCommandList.h:49`）——纯虚命令录制接口，后端把每条翻译为 `vkCmdXxx` 或 `glXxx`：

| 分组 | 方法 | 行号 |
|---|---|---|
| RenderPass | `BeginRenderPass`/`EndRenderPass` | `:57/58` |
| 视口/裁剪 | `SetViewport`/`SetScissor` | `:63/64` |
| 绑定 | `BindPipeline`/`BindVertexBuffer`/`BindIndexBuffer`/`BindResourceSet`/`PushConstants` | `:69-87` |
| 绘制 | `Draw`/`DrawIndexed` | `:92/97` |
| 计算 | `Dispatch`（默认空） | `:111` |
| 屏障 | `PipelineBarrier`（默认空） | `:121` |
| 光追 | `BuildAccelerationStructure`/`TraceRays`（默认空） | `:134/146` |

- `BindResourceSet.setIndex` 对齐 VK descriptor set，GL 后端按 reflection 表映射回具体 slot（`:77-81` 注释）。
- 计算/屏障/光追均给默认空实现，子类（`GLCommandList`/`VKCommandList`/`GDeviceHeadless`/`GDeviceMainThread`）按需 override。
- `AccelerationStructureBuildInfo`（`:28`）承载一次命令流内 BLAS/TLAS 构建/refit 的几何与 instance 引用。

---

## 8. 线程模型三件套

### 8.1 `GThreadingMode.h`
`enum class GThreadingMode : uint8_t`（`:13`）：`Direct`（GL 默认）/`NonThreaded`/`Threaded`（VK 默认）。

### 8.2 主线程门面 · `GDeviceMainThread.{h,cpp}`
`class GDeviceMainThread final : public GThreadableDevice`（`GDeviceMainThread.h:27`），构造时接管一个 `unique_ptr<GDevice> mRealDevice`（`:148`）。
- **帧控制**（`:37-45`）：`BeginFrame/Submit/Present/WaitIdle` 经 `PushFrameCmd(kind)`（`:146`）序列化为 `GCommand` 写入 `mStream`（`CommandRingBuffer`），由 Worker 消费。
- **资源 API**（`:48-73`）：`Create*/Destroy/Update*` 持 `mResourceMutex` 锁（`:151`）**同步透传**到 `mRealDevice`（M2 最小实现）。
- 资源查询（`:76-78`）转发 RealDevice，避免上层看到空表。
- 余下 `*Impl()`（`:89-142`）永不被调用（因为公共 API 已在 Client 层 override）。

### 8.3 工作线程 · `GDeviceWorker.{h,cpp}`
`class GDeviceWorker`（`GDeviceWorker.h:46`）：不持有 device 所有权，从共享 `CommandRingBuffer` 读命令派发到 RealDevice。
- `enum class GCommandKind`（`:30`）：`Stop/BeginFrame/Submit/Present/WaitIdle`；`GCommandHeader`（`:40`）仅 4 字节头。
- `Start`/`Stop`（`:58/60`）：起停线程；`Stop` 写入 Stop 命令并 join。
- `WaitForTick(waitTick)`（`:65`）：主线程帧同步点——`mTickProgress >= waitTick` 时返回；`IncrementTick`（`:67`）Client push 一条命令时递增。
- 私有 `Run`/`DispatchCommand`（`:70/71`）：Worker 循环读命令并 dispatch；成员含 `mThread`、`mRunning`、`mTickCount`/`mTickProgress`（atomic）、`mTickMutex`/`mTickCv`。

### 8.4 命令流 · `CommandRingBuffer.{h,cpp}`
SPSC（单生产单消费）字节环形流，Client 写、Worker 读；承载帧命令的序列化字节。

---

## 9. 调度与 Pass

### 9.1 `PassScheduler.{h,cpp}` ★统一帧流程
`class PassScheduler`（`PassScheduler.h:18`）仅依赖 `IGDevice`/`RenderCommandList`/`IRenderPass`，**两后端流程完全一致**。
- `AddPass`（`.cpp:14`）：加入后 `SortPasses`（`.cpp:26`）按 `passEvent` 值 `stable_sort`。
- `InitAllPasses`/`DestroyAllPasses`（`.cpp:34/40`）：统一用 `IGDevice&` 驱动 Pass 资源生命周期。
- `DrawFrame`（`.cpp:47`）核心流程：
  1. `BeginFrame()`（后端内部做 Acquire/等 Fence）
  2. `AcquireCommandList()`；为 nullptr（如 swapchain out-of-date）则仅 `++mFrameCounter` 返回
  3. 取 `frameIndex = GetCurrentFrameIndex()`
  4. 遍历 Pass：`Update(device, frameIndex)` + `Record(device, cmd, frameIndex, imageIndex)`
  5. `Submit(cmd)` → `RenderImGuiOverlay()` → `Present()` → `++mFrameCounter`

> **ImGui 时序坑**（`.cpp:74-84` 注释）：`RenderImGuiOverlay` 必须在 `Submit` **之后**调用。GL 的 imgui 是 immediate 模式，若早于 Submit 会被 Submit 内的 `glClear`/场景绘制覆盖；VK 端已把 imgui 录制挪进 `SubmitImpl` 的 `End()` 之前，故这里 Submit 后再调是 no-op。此坑详见 `99_Pitfalls.md`。

### 9.2 `IRenderPass.h`
- `enum class ERenderPassEvent : int`（`:20`）：`BeforeRendering=0 … ShadowMap=75 … GBuffer=175 … Lighting=275 … OpaqueShading=375 … PostProcess=575 … FinalBlit=675 … AfterRendering=700`，整数值决定排序。
- `class IRenderPass`（`:51`）：`Init`（`:57`）/`Destroy`（`:60`）只收 `IGDevice&`；`Update`（`:63`，默认空）；`Record`（`:66`，纯虚，收 `IGDevice& + RenderCommandList&`）；`passEvent` 字段（`:72`）。业务 Pass 继承本类即可两后端复用。

### 9.3 `IWindow.h`
后端无关窗口抽象（尺寸/标题/关闭状态查询），GLFW 实现在 `Platform/`。

---

## 10. 内部支撑

| 文件 | 内容 |
|---|---|
| `HandleAllocator.h` | 单调递增分配 `uint64_t` 句柄 id（0 保留为非法），供基类 `Create*` 使用 |
| `GContextData.h` | 设备上下文数据（帧内共享状态） |
| `GResources.h` | 后端无关元数据包装：`RHIBuffer`/`RHITexture`/`RHIShader`（仅含 desc+handle），供 `FindBuffer/FindTexture/FindShader` 反查 |
| `GStateCache.h` | `SamplerCache`/`PipelineCache`：desc→handle 去重映射，`CreateSampler/CreatePipeline` 先查表命中复用 |
| `GDeviceHeadless.h` | `GDeviceHeadless`（继承 `GDevice`）：所有 `*Impl` 空实现，用于单测/Headless CI，不依赖 GPU/窗口 |
| `GDeviceFactory.{h,cpp}` | Core 侧工厂（Interface 侧另有一份分发器，见 `40_Interface.md`） |

---

## 11. 着色器 / 材质 / 资产

| 文件 | 核心类型 | 职责 |
|---|---|---|
| `ShaderReflection.h` | 反射信息入口 | 沿用 `GDescs.h` 的 `ReflectionInfo`/`ResourceBinding` |
| `ShaderReflector.{h,cpp}` | `ShaderReflector` | 从字节码/源码解析出反射信息（binding/pushConstant） |
| `ShaderAsset.{h,cpp}` | `ShaderAsset` | 着色器资产：编译产物 + 反射，供 `CreateShader` 使用 |
| `ShaderParameterSet.h` | 参数集 | 着色器可配置参数集合 |
| `Material.{h,cpp}` | `Material` | 绑定 Pipeline/Shader + 属性，对接 `ShaderParameterSet` |
| `MaterialInstance.h` | `MaterialInstance` | 材质实例（覆盖部分属性） |
| `GpuMesh.h` | `GpuMesh` | 顶点/索引 Buffer 句柄 + 布局 |
| `GpuModel.{h,cpp}` | `GpuModel` | 多 `GpuMesh` + 材质的模型 |
| `AssetGpuUploader.{h,cpp}` | `AssetGpuUploader` | 把 CPU 侧资产（`AssetLoader`）上传为 GPU 资源（`CreateBuffer/Texture` + `UpdateBuffer/Texture`） |
| `RayTracingManager.{h,cpp}` | AS 管理器 | 管理 BLAS/TLAS 生命周期与构建（配合 `CreateAccelerationStructure` / `BuildAccelerationStructure`） |

> 这批高层封装均只依赖 `IGDevice` 抽象，是"后端无关"承诺得以成立的证据。
