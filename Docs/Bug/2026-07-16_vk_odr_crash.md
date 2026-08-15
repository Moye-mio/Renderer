# Bug 记录

> 事故原文。提炼后的设计约束见 [`99_Pitfalls.md`](../Architecture/99_Pitfalls.md) 第 1 条。

## Bug：VK 后端启动 001 项目崩溃（0xC0000005 访问违规）

- 日期：2026-07-16
- 影响范围：所有通过 `RendererInterface`（`::TitusGfx` API）以 `--backend=vk` 启动的业务工程（如 `001_Reflective_shadow_map`）
- 现象：进程启动即崩溃，退出码 **-1073741819（0xC0000005，内存访问违规）**；OpenGL 后端（`--backend=gl`）一切正常。

### 根因：`VKDevice` 类的 ODR（单一定义规则）违规导致堆破坏

`RendererVK/VKDevice.h` 中存在受预处理宏 `RENDERER_ENABLE_RAY_TRACING` 控制的成员，会改变类的 `sizeof` 与成员偏移：

```cpp
// RendererVK/VKDevice.h : 388-391
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追：加速结构映射表
        std::unordered_map<uint64_t, VKAccelStructEntry>  mAccelStructs;
#endif
        // 其后还有 mImGuiCallback / mImGuiUserData 等成员
```

而该宏在各工程中定义不一致：

| 工程 | 是否定义 `RENDERER_ENABLE_RAY_TRACING` | 是否 include `VKDevice.h` |
|---|---|---|
| `RendererVK.vcxproj` | ✓ 定义 | ✓（构造函数/成员函数在此编译，**大布局**） |
| `RendererInterface.vcxproj` | ✗ 未定义（修复前） | ✓（`GDeviceFactory.cpp` 执行 `new VKDevice`，**小布局**） |
| `RendererCore.vcxproj` | ✗ 未定义（修复前） | — |

崩溃调用链：

1. `001/main.cpp` → `APP::InitApp()`
2. → `RendererInterface/GDeviceFactory.cpp:46`：`new ::TitusVkGraphics::VKDevice()`
3. 这行 `new` 编译在 **RendererInterface** 编译单元，未定义 RT 宏，按 **较小布局** 计算 `sizeof(VKDevice)` 申请堆内存；
4. 但 `VKDevice` 的构造函数与成员函数编译在 **RendererVK** 编译单元，定义了 RT 宏，按 **较大布局**（多出 `mAccelStructs` 及其后成员）初始化 / 读写；
5. 结果“**按小尺寸分配、按大布局构造与使用**”——构造时越界写堆，后续按错位偏移解引用 `mContext` / `mSwapchain` 等 → **访问违规 0xC0000005**。

GL 后端的 `GLDevice` 不含任何受该宏影响的条件成员，布局在各编译单元一致，故不受影响、可正常运行。

### 修复

让 `RENDERER_ENABLE_RAY_TRACING` 在所有会 include `VKDevice.h` 的工程中保持一致——为 `RendererInterface.vcxproj` 与 `RendererCore.vcxproj` 的 **Debug/Release** 配置补上该宏：

```
...;RENDERER_ENABLE_VK;RENDERER_ENABLE_GL;RENDERER_ENABLE_RAY_TRACING;GLFW_INCLUDE_NONE;...
```

改动后需 **重新编译** `RendererCore`、`RendererInterface` 及其依赖工程（静态库，宏改动仅在重编后生效）。

### 教训

1. **凡是用预处理宏控制类成员（改变 `sizeof`/成员偏移）的头文件，该宏必须在所有引用它的编译单元中保持一致**；否则不同 TU 看到不同类布局，构成 ODR 违规，`new`/构造/成员访问尺寸错位，导致堆破坏与访问违规。
2. 这类 bug 编译期无报错、运行期才崩，排查成本高。
3. **建议**：把 `RENDERER_ENABLE_VK / RENDERER_ENABLE_GL / RENDERER_ENABLE_RAY_TRACING` 等影响公共头文件布局的宏，统一抽到一个共享的 `.props` 文件集中定义，从源头杜绝各工程宏不一致。
