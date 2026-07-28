# RendererGL 详解（OpenGL 后端）

> 逐文件 → class/struct → 职责 + 热点函数下钻（带行号）。
> 命名空间：`TitusGraphics`（依赖 `TitusRHI` = `../RendererCore`）。
> 定位：用 OpenGL 实现 `RendererCore` 的 `GDevice` 契约，是 `--backend=gl` 的落地；线程默认 `Direct`。

---

## 0. 定位：单一"设备实现"职责

> **历史说明**：早期 `RendererGL` 曾内含一整套独立 OpenGL 渲染引擎——`App`/`Interface`/`RenderPassScheduler`/`ResourceManager`/`IRenderPass`/`RenderCommandBuffer` + `Camera`/`Mesh`/`Model`/`ShaderProgram`/`GLUtils`/`GLFWWindow`/`MainGUI` 等。随着业务 Pass 全部迁移到 `RendererCore::IRenderPass` 统一基类、调度收敛到 `RendererCore::PassScheduler`，这套旧骨架已**整体清退删除**。
>
> 本库现在只承担唯一职责：**用 OpenGL 落地 `RendererCore` 的 `GDevice` 契约**。所有后端无关能力（Pass 调度、资源/模型、相机、输入、ImGui、窗口抽象）都在 `RendererCore` / `RendererInterface` / `Platform`。

---

## 1. 文件全景

| 文件 | 一句话职责 |
|---|---|
| `GLDevice.{h,cpp}` | `GThreadableDevice` 的 OpenGL 实现（设备主体） |
| `GLCommandList.{h,cpp}` | `RenderCommandList` 的 OpenGL 实现（延迟队列 + 回放） |
| `GLTranslate.{h,cpp}` | Core 枚举 → GL 枚举翻译 |
| `GLDeviceFactory.cpp` | 桥接创建入口（由 `RendererInterface::GDeviceFactory` 调用）|

> 就这 4 个编译单元。窗口由 `RendererCore::IWindow`（`Platform` 提供实现）承担；模型/网格由 `RendererCore` 的 `GpuModel`/`GpuMesh` + `AssetLoader` 承担；相机/输入/ImGui 由 `RendererInterface` 的 `CAMERA`/`INPUT_MANAGER`/`IMGUI` 承担（imgui 源码由 `RendererInterface.vcxproj` 统一编译）。

---

## 2. 设备实现 · `GLDevice.{h,cpp}` ★

`class GLDevice`（`GLDevice.h:89`）继承 `TitusRHI::GThreadableDevice`，负责 GLEW 初始化、能力查询、"`GHandle` ↔ `GLuint`"映射表，以及全部 `*Impl()` 钩子。

### 2.1 资源条目 struct（`GLDevice.h`）
| struct | 行号 | 关键字段 |
|---|---|---|
| `GLBufferEntry` | `:26` | `GLuint id`、`GLenum target/usage`、`uint64_t size`、`bool mappable`、`void* persistentPtr` |
| `GLTextureEntry` | `:36` | `GLuint id`、`GLenum target/internalFmt/dataFmt/dataType`、`width/height/depth`、`mipLevels/arrayLayers`、`bool isDepth` |
| `GLSamplerEntry` | `:48` | `GLuint id` |
| `GLShaderEntry` | `:53` | `GLuint id`、`GLenum stage`、`ReflectionInfo reflection` |
| `GLPipelineEntry` | `:60` | `GLuint program`、`topology`、光栅/深度/混合 state、`VertexLayout`、`resourceBindings`、`pushConstantRanges`、`GLuint vao`、`bool isCompute` |
| `GLRenderTargetEntry` | `:77` | `GLuint fbo`、`width/height`、`colorAttachments`、`depthStencilAttachment` |

`class GLDevice`（`:89`）成员：`unique_ptr<GLCommandList> mCommandList`（复用的命令列表）、6 张 `unordered_map<uint64_t, GLXxxEntry>` 映射表、`mDefaultWidth/Height`（后缓冲尺寸）、`mImGuiCallback/mImGuiUserData`（后端无关的 Overlay 回调指针，实际 imgui 调用在 `RendererInterface`）。

### 2.2 钩子实现（`GLDevice.cpp`）
| 钩子 | 行号 | 内部逻辑 |
|---|---|---|
| `OnInitBackend` | `:62` | 校验 window → `glewExperimental=TRUE`+`glewInit` → `glClipControl(LOWER_LEFT, ZERO_TO_ONE)`（对齐 VK NDC）→ `glEnable(GL_FRAMEBUFFER_SRGB)` → `FillCaps()` → 建 `mCommandList` |
| `OnInitSwapchain` | `:112` | GL 无真交换链：记录 window 宽高到 `mDefaultWidth/Height`，设默认 `glViewport` |
| `FillCaps` | `:190` | `glGetIntegerv` 查最大纹理尺寸等填 `mCaps` |
| `CreateBufferImpl` | `:215` | `PickGLBufferTarget/UsageHint` → 记录 size/mappable → `glGenBuffers`+`glBindBuffer`+`glBufferData`（含 initialData）→ 写 `mBuffers` |
| `CreateTextureImpl` | `:252` | 按 `TextureType` 选 target → `ToGLInternalFormat/DataFormat/DataType` 翻译 → mip=0 时展开 `floor(log2(max))+1` → `glGenTextures`+`glTexStorage2D/3D` → 写 `mTextures` |
| `CreateSamplerImpl` | `:363` | `glGenSamplers` → 按 `SamplerDesc` 设 min/mag 滤波、wrap、LOD、compare、各向异性（`GLEW_EXT_texture_filter_anisotropic` 探测）→ 写 `mSamplers` |
| `CreateShaderImpl` | `:400` | `glCreateShader`+源码 `glShaderSource`+`glCompileShader`（单阶段），连同 `ReflectionInfo` 写 `mShaders` |
| `CreatePipelineImpl`(Graphics) | `:441` | 拷贝 state → `glCreateProgram`+attach VS/FS/GS+`glLinkProgram` → 建 VAO 配 `glVertexAttribFormat/Binding/BindingDivisor` → 反射映射 UBO block/sampler/image unit → 写 `mPipelines` |
| `CreatePipelineImpl`(Compute) | `:564` | 校验 CS 阶段 → `isCompute=true` → attach CS+link → 同 graphics 反射映射 → 写 `mPipelines` |
| `CreateRenderTargetImpl` | `:636` | `glGenFramebuffers`+bind → 逐 colorAttachment `glFramebufferTexture2D` 收集 drawBuffers → 处理 depthStencil → `glDrawBuffers` → `glCheckFramebufferStatus` 校验 |
| `UpdateBufferImpl` | `:241` | `glBindBuffer` → `glBufferSubData(dstOffset, bytes, src)` |
| `UpdateTextureImpl` | `:328` | `glTexSubImage2D/3D` 按 mip/offset/尺寸上传；若 storage 有多 mip 且上传 mip0，`glGenerateMipmap` 补全 mip 链（避免 incomplete texture） |
| `BeginFrameImpl` | `:696` | `mCommandList->Reset()` 清空录制队列 |
| `AcquireCommandListImpl` | `:701` | 返回复用的 `mCommandList.get()` |
| `SubmitImpl` | `:706` | `mCommandList->Replay()` 在主线程回放全部命令 |
| `PresentImpl` | `:711` | 空实现（`SwapBuffers` 由 IWindow 完成，帧索引推进在基类） |
| `OnShutdownBackend` | `:131` | `glFinish` 后逐类删除 program/vao/shader/sampler/fbo/texture/buffer 并清表 |
| `OnWaitIdleImpl` | `:174` | 退化为 `glFinish()` |

> 表中未逐一列出的 `DeleteBufferImpl/DeleteTextureImpl/DeleteSamplerImpl/DeleteShaderImpl/DeletePipelineImpl/DeleteRenderTargetImpl`（`GLDevice.h:154-159`）与对应 `Create*Impl` 对称：查表 → `glDelete*` → 擦除条目；`OnShutdownBackend`（`:131`）即逐类批量执行这些删除。

> **GL 后端的本质**：OpenGL 没有 immutable PSO、没有交换链、没有显式同步。因此 `GLPipelineEntry` 用 `Program + VAO + 状态块`模拟 PSO；`SubmitImpl` 靠 `GLCommandList::Replay()` 把录制的 `std::function` 队列在主线程一次性回放；`PresentImpl` 为空。这也解释了为何 GL 默认 `Direct` 线程模式。

---

## 3. 命令录制 · `GLCommandList.{h,cpp}`

`RenderCommandList` 的 GL 实现：内部自持一个 `std::function` **延迟队列**——录制期只把每条命令封成 lambda 入队（不立即调 GL），`Reset()` 清队列，`Replay()` 在 `SubmitImpl` 时顺序执行。

- `BeginRenderPass` = "Bind FBO + 按 `LoadOp` 选择性 `glClear` + 按 `StoreOp` 选择性 `glInvalidateFramebuffer`"（见 `RenderCommandList.h:4-8` 注释）。
- `BindResourceSet` 按 `ReflectionInfo` 把 `setIndex/binding` 映射回 GL 具体 slot（UBO binding / texture unit）。
- `PushConstants` 转 `glUniform*`（借助 `PushConstantRange.glName`）。
- `Dispatch→glDispatchCompute`、`PipelineBarrier→glMemoryBarrier` 位掩码。

---

## 4. 枚举翻译 · `GLTranslate.{h,cpp}`

`Format→(internalFormat, dataFormat, dataType)` 三元组、`PrimitiveTopology→GLenum`、`BlendFactor/BlendOp→GL*`、`CompareOp→GL*`、`CullMode/FrontFace/PolygonMode→GL*`、`FilterMode/MipmapMode/AddressMode→GL*` 等。是"抽象枚举 ↔ OpenGL"的唯一翻译点。

---

## 5. 后端无关能力的去向

清退旧骨架后，以下能力不再由 RendererGL 承担，改由更合理的分层提供：

| 曾在 RendererGL 的旧能力 | 现在的归属 |
|---|---|
| 主循环 / 对外 API（`App`/`Interface`） | `RendererInterface` 的 `APP` 门面 |
| Pass 抽象 + 调度（`IRenderPass`/`RenderPassScheduler`） | `RendererCore` 的 `IRenderPass` + `PassScheduler` |
| 命令队列（`RenderCommandBuffer`） | 已并入 `GLCommandList` 自身实现 |
| 资源/共享数据（`ResourceManager`） | `RendererInterface` 资源管理 + `RESOURCE_MANAGER` |
| 相机 / 输入（`Camera`/`InputManager`） | `RendererInterface` 的 `CAMERA` / `INPUT_MANAGER`（见 `40_Interface.md`）|
| 模型 / 网格（`Model`/`Mesh`） | `AssetLoader`（CPU 解码）+ `RendererCore` 的 `GpuModel`/`GpuMesh` |
| 着色器（`ShaderProgram`/`ShaderModule`） | `GLDevice::CreateShaderImpl` + `CreatePipelineImpl` |
| GUI（`MainGUI`/`IGUI` + imgui 源码）| `RendererInterface` 的 `IMGUI`（imgui 源码由 `RendererInterface.vcxproj` 统一编译）|
| 窗口（`GLFWWindow`）| `RendererCore::IWindow` + `Platform` 的实现 |
