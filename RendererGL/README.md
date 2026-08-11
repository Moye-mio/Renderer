# RendererGL —— OpenGL Backend

使用 **OpenGL** 实现 `RendererCore` 的 `GDevice` 契约的渲染后端。是 `--backend=gl` 的落地。

> **历史说明**：早期本目录曾内含一整套独立 OpenGL 引擎（`App` / `Interface` / `RenderPassScheduler` /
> `IRenderPass` 等）。业务 Pass 迁移到 `RendererCore::IRenderPass` 后，旧骨架已清退。
> 本库现在只承担「OpenGL 设备实现」这一职责。

## 目录结构

```
RendererGL/
├─ GLDevice.{h,cpp}           # GThreadableDevice 的 OpenGL 实现
├─ GLCommandList.{h,cpp}      # RenderCommandList：std::function 延迟队列 + Replay
├─ GLTranslate.{h,cpp}        # Core 枚举 → GLenum
├─ GLError.h                  # GL 错误检查辅助
├─ GLDeviceFactory.cpp        # 桥接创建入口（由 RendererInterface::GDeviceFactory 调用）
└─ RendererGL.vcxproj
```

命名空间：`TitusGraphics`。

## 使用方式

业务侧**不直接使用** RendererGL，而是通过 `RendererInterface` 的 `APP` 门面 + 继承
`TitusRHI::IRenderPass`。选择 OpenGL 只需 `--backend=gl`。

默认线程模式为 **`Direct`**（与 VK 相同）。

## 与 Vulkan 后端的关键差异

1. **隐式上下文 / 交换链**：依赖 GLFW GL context + `SwapBuffers`；`PresentImpl` 为空
2. **延迟命令队列**：录制封成 `std::function`，`Submit` 时 `Replay()`
3. **无 immutable PSO**：用 Program + VAO + 状态块模拟
4. **GLSL 源码直喂**：`glShaderSource`（VK 侧为 SPIR-V）

更详尽的逐文件下钻见 [`Docs/Architecture/30_RendererGL.md`](../Docs/Architecture/30_RendererGL.md)。
