# RendererVK —— Vulkan Backend

使用 **Vulkan** 实现 `RendererCore` 的 `GDevice` 契约的渲染后端。是 `--backend=vk` 的落地。

> **历史说明**：早期 RendererVK 曾与 OpenGL 版对称地内含一整套独立渲染引擎
> （`VkApp`/`VkInterface`/`VkPassScheduler`/`VkResourceManager`/`IVkRenderPass`，
> 以及 `VkShaderModuleWrapper`/`VkGraphicsPipeline` 等封装）。业务 Pass 全部迁移到
> `RendererCore::IRenderPass` 统一基类后，这套旧骨架已整体清退删除。本库现在只承担
> "Vulkan 设备实现"这一单一职责。

## 目录结构

```
RendererVK/
├─ Common.{h,cpp}                     # 全局配置 / VK_CHECK / 队列族 / 交换链支持
├─ RENDERER_VK_EXPORTS.h              # 导出宏
├─ VkWindow.{h,cpp}                   # GLFW 窗口 + VkSurface 创建
├─ VkContext.{h,cpp}                  # Instance / PhysicalDevice / Device / Queue（+ 光追探测）
├─ VkSwapchainWrapper.{h,cpp}         # 交换链 + Depth + 默认 RenderPass + Framebuffer
├─ VkCommandBufferWrapper.{h,cpp}     # Primary/Secondary CommandBuffer 封装
├─ VKDevice.{h,cpp}                   # GThreadableDevice 的 Vulkan 实现（设备主体）
├─ VKCommandList.{h,cpp}              # RenderCommandList 的 Vulkan 实现
├─ VKTranslate.{h,cpp}                # Core 枚举 → Vulkan 枚举翻译
├─ VKShaderCompiler.{h,cpp}           # GLSL/HLSL → SPIR-V 编译（+ 反射）
└─ VKDeviceFactory.cpp                # 桥接创建入口（由 RendererInterface::GDeviceFactory 调用）
```

> `VKDevice` 内部直接 `vkCreateGraphicsPipelines`/`vkCreateShaderModule` 创建管线与
> 着色器模块，不再有独立的 `VkGraphicsPipeline`/`VkShaderModuleWrapper` 封装类。

## 依赖

| 组件 | 来源 | 路径 |
|---|---|---|
| Vulkan SDK 1.2+ | LunarG 官方 | `VULKAN_SDK`（安装器写入） |
| GLFW 3.x       | `Third-Party/OpenGL/` | `$(OPENGL)`（`Directory.Build.props`） |
| GLM            | `Third-Party/glm/` | `$(GLM)`（`Directory.Build.props`） |

安装 [Vulkan SDK](https://vulkan.lunarg.com/) 后，环境变量 `VULKAN_SDK` 会自动设置。

## 使用方式

业务侧**不直接使用** RendererVK，而是通过 `RendererInterface` 的 `APP` 门面 + 继承
`::TitusRHI::IRenderPass` 的后端无关 Pass。选择 Vulkan 后端只需 `--backend=vk`。
参考 `Examples/Test_001_VkTriangle`（默认 VK）或 `Test_000_UnifiedTriangle`：

```cpp
#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxPass.h"

int main(int argc, char** argv)
{
    using namespace ::TitusRHI;
    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown) APP::SetBackend(GBackend::Vulkan);

    APP::InitApp();
    APP::AddPass(std::make_shared<TrianglePass>());   // TrianglePass : public ::TitusRHI::IRenderPass
    while (!APP::ShouldClose()) APP::UpdateApp();
    APP::WaitIdle();
    APP::ShutdownApp();
}
```

## Shader 工作流

Vulkan 只接受 SPIR-V 字节码。示例工程的 `Shader/CompileShaders.bat` 已配置为
PreBuildEvent 自动调用 `glslc` 把 `.vert/.frag` 编译为 `.spv`。业务 Pass 在
`Init(IGDevice&)` 中读取 `.spv` 字节，经 `device.CreateShader(ShaderDesc)` 建模块
（VK 后端内部走 `VKShaderCompiler` / `vkCreateShaderModule`）。

## 帧绘制流程（由 RendererCore 统一驱动）

帧循环不再由 VK 自有调度器承担，而是由后端无关的 `RendererCore::PassScheduler` 驱动，
GL/VK 共用同一份逻辑：

```
APP::UpdateApp() 每帧:
  └─ PassScheduler::DrawFrame()
       ├─ IGDevice::BeginFrame()          (VK: vkWaitForFences + vkAcquireNextImageKHR)
       ├─ IGDevice::AcquireCommandList()
       ├─ 遍历 Pass::Update / Pass::Record (按 ERenderPassEvent 顺序)
       ├─ IGDevice::Submit(cmd)           (VK: primaryCmd->End + vkQueueSubmit)
       └─ IGDevice::Present()             (VK: vkQueuePresentKHR)
```

## 与 OpenGL 后端的关键差异

1. **显式管理上下文**：`VkInstance/VkDevice/VkQueue` 必须手工创建/销毁
2. **显式交换链**：不再依赖 `glfwSwapBuffers`，而是 Acquire/Present
3. **显式同步**：Fence 保证 CPU-GPU，Semaphore 保证 GPU-GPU
4. **Pipeline 固化**：OpenGL 的 Program + 状态机 → Vulkan 的 immutable Pipeline
5. **Shader 预编译**：GLSL 源码 → SPIR-V 字节码
6. **命令缓冲显式化**：`VkCommandBuffer`（GL 侧为 `std::function` 延迟队列）

> 更详尽的逐文件下钻见 `Docs/Architecture/20_RendererVK.md`。
