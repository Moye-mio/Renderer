# 设计陷阱与约束（第 4 遍）

> 把踩过的坑固化为"设计约束"，避免重犯。第 1 条源自 [`2026-07-16_vk_odr_crash.md`](../Bug/2026-07-16_vk_odr_crash.md) 的真实事故。

---

## 1. 宏控制类布局 → ODR 违规 → 堆破坏（0xC0000005）★

### 现象
所有经 `RendererInterface`（`::TitusRHI` API）以 `--backend=vk` 启动的工程，进程启动即崩，退出码 `-1073741819`（0xC0000005 访问违规）；GL 后端一切正常。

### 根因
`RendererVK/VKDevice.h:388-391` 有受 `RENDERER_ENABLE_RAY_TRACING` 宏控制的成员：

```cpp
#if defined(RENDERER_ENABLE_RAY_TRACING)
    std::unordered_map<uint64_t, VKAccelStructEntry> mAccelStructs;
#endif
    // 其后还有 mImGuiCallback / mImGuiUserData
```

该宏会改变 `VKDevice` 的 `sizeof` 与成员偏移；而它在各工程定义不一致：

| 工程 | 定义 RT 宏 | include `VKDevice.h` |
|---|---|---|
| `RendererVK.vcxproj` | ✓（构造函数在此编译，**大布局**） | ✓ |
| `RendererInterface.vcxproj` | ✗（修复前，`new VKDevice` 在此，**小布局**） | ✓ |
| `RendererCore.vcxproj` | ✗（修复前） | — |

崩溃链：`GDeviceFactory.cpp:46` 的 `new VKDevice()` 在 **Interface** 编译单元按**小尺寸**分配堆内存，但构造函数在 **RendererVK** 编译单元按**大布局**初始化 → "按小尺寸分配、按大布局构造" → 越界写堆 → 后续按错位偏移解引用 `mContext/mSwapchain` → 访问违规。GL 的 `GLDevice` 不含此类条件成员，故不受影响。

同类布局宏也存在于 `VkContext.h`（`RayTracingFunctions`、`m_rtFunctions` 等受同一宏控制，见 `20_RendererVK.md §3`）。

### 修复
让 `RENDERER_ENABLE_RAY_TRACING` 在**所有会 include `VKDevice.h` 的工程**中一致——为 `RendererInterface.vcxproj` 与 `RendererCore.vcxproj` 的 Debug/Release 补上该宏，并**重编**相关静态库。

### 约束（务必遵守）
1. **凡是用预处理宏控制类成员（改变 `sizeof`/偏移）的头文件，该宏必须在所有引用它的编译单元中保持一致**，否则构成 ODR 违规。
2. 此类 bug 编译期无报错、运行期才崩，排查成本高。
3. **建议**：把 `RENDERER_ENABLE_VK/GL/RAY_TRACING` 等影响公共头布局的宏，统一抽到共享 `.props` 集中定义，从源头杜绝各工程不一致。

---

## 2. ImGui Overlay 必须在 `Submit` 之后录制/绘制

`PassScheduler::DrawFrame`（`PassScheduler.cpp:74-88`）中 `RenderImGuiOverlay()` 排在 `Submit` **之后**：

- **GL**：业务 Pass 只把命令录成 lambda 入队，`Submit` 触发 `Replay()` 才真正提交给 driver；而 `imgui_impl_opengl3` 是 immediate 模式（一调用立即提交）。若在 Submit 之前调 imgui，会先于业务画面打到默认 FB，随后 Submit 内的 `glClear`/场景绘制把它覆盖 → 最终 `SwapBuffers` 出去只剩场景没有 GUI。
- **VK**：imgui draw 必须录进 primary cmdbuf，但 `SubmitImpl` 内会调 `primaryCmd->End()` 关闭 cmdbuf；所以 VK 把 imgui 录制**挪进 `SubmitImpl` 内、`End()` 之前**。因此 `DrawFrame` 里 Submit 后再调 `RenderImGuiOverlay` 时，VK 实现已是 no-op，不会重复执行。

### 约束
新增后端或改动帧流程时，务必保持"Overlay 在 Submit 之后由 `PassScheduler` 统一调度"这一约定；后端只负责"在正确时机以正确的 FB/RenderPass 状态调用回调"。

---

## 3. 抽象层不可被穿透（依赖方向 + 无后端头）

由 CI 脚本在构建期强制（见 `00_Overview.md §6`）：

- `RendererCore/*` 与业务侧禁止 include `<vulkan/...>`、`<GL/...>`、`<glad/...>`、`<GLFW/glfw3.h>`（脚本 `Tools/check_no_backend_headers.py/.bat`，Examples PreBuild 命中即失败）。
- 依赖方向单向（脚本 `Tools/check_deps_direction.bat`，Interface PreBuildEvent）：
  - `RendererCore/*` 禁 include `RendererGL/`、`RendererVK/`、`RendererInterface/`
  - `RendererGL/*` 禁 include `RendererVK/`、`RendererInterface/`
  - `RendererVK/*` 禁 include `RendererGL/`、`RendererInterface/`
  - `Platform/*` 禁 include 以上全部
- **唯一例外**：`RendererInterface/GDeviceFactory.cpp` 允许同时 include `GLDevice.h` 与 `VKDevice.h`（后端分发点）。

### 约束
新增后端强相关代码放到对应后端目录，通过 `GDeviceFactory` 与 `IGDevice`/`RenderCommandList` 抽象注入，绝不在 `RendererCore` 或业务侧引入后端头。

---

## 4. `AcquireCommandList` 返回指针的生命周期

`IGDevice::AcquireCommandList`（`IGDevice.h:91-93`）返回的 `RenderCommandList*` **由设备管理**：
- 调用方**不得 `delete`**；
- `Submit` 之后必须视为**失效**，不可再录制或缓存跨帧使用。

GL 后端复用同一个 `mCommandList` 对象（`GLDevice.cpp:701`），跨帧持有它更会导致错乱。

---

## 5. Frames-in-Flight 与延迟销毁

不要在 `Destroy(handle)` 后假设资源立即释放：基类只入队（`EnqueueDestroy`），要等 `submitFrame + framesInFlight <= 当前帧` 才真正 `Delete*Impl`（`GDevice.h:230-240`）。反之，**不要绕过 `Destroy` 直接释放后端对象**，否则可能释放仍被 GPU 在用的资源。

---

## 6. 后端能力差异需以 `GCaps` / `IsValid` 判定，勿假设

- Compute/RayTracing 管线、加速结构等 `*Impl` 在不支持的后端默认返回 `false`/invalid 句柄（`GDevice.h:185/191/200`）。上层必须以返回句柄的 `IsValid()` 判定，而非假设某后端一定支持。
- VK 是否支持光追由 `VkContext` 运行期探测（`SupportsRayTracing` 等），且受 `RENDERER_ENABLE_RAY_TRACING` 宏门控。

---

## 7. Tracy：勿把「Capture 关闭」当成零开销，也勿绕过门控

完整说明见 `50_Tracy.md`。要点：

1. **测真实帧率**用编译期关 Tracy（`/p:TitusTracyEnable=false` 或 Release 默认），不要只靠 ImGui「Tracy Capture」。
2. 开启 Tracy 时 props 强制 `/Zi`；Edit and Continue `/ZI` 下 `__LINE__` 非常量 → `ZoneScoped` **C2131**。
3. `ZoneTransient*(..., active)` / `ZoneNamed*(..., active)` 的 `active` 必须传 `TitusTracyCaptureEnabled()`，写死 `true` 会绕过 ImGui 开关。
4. 改 `TitusTracyEnable` 后需大范围重编；`TRACY_ENABLE` 不一致属于预处理级混链风险。
