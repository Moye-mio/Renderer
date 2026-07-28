# RendererCore

后端无关的核心层（RHI 风格的图形设备抽象，与 UE RHI / bgfx 等跨后端渲染器思路一致）。它定义"做什么"而不实现"怎么做"，
具体后端（Vulkan / OpenGL）作为可插拔实现位于：

- [`RendererGL/`](../RendererGL)（OpenGL 后端）
- [`RendererVK/`](../RendererVK)（Vulkan 后端）

业务侧 Pass、示例工程、`Material/Mesh/Camera` 等高层封装**只依赖本模块**（经
`RendererInterface` 门面转发），不感知底层是 OpenGL 还是 Vulkan。

## 核心头文件

| 文件 | 作用 |
| --- | --- |
| `GHandle.h`          | 不透明、类型安全的 GPU 资源句柄（`BufferHandle/TextureHandle/...`） |
| `GEnums.h`           | 后端无关枚举（`Format / PrimitiveTopology / BlendFactor / LoadOp / ...`） |
| `GDescs.h`           | 资源描述结构体与管线状态结构体 |
| `IGDevice.h`         | 后端无关的设备接口（资源创建 / 数据上传 / 帧控制 / 能力查询） |
| `GDevice.h`          | 后端无关基类：模板方法（参数校验 + 句柄分配 + 延迟销毁 + 流程编排），子类只实现 `*Impl()` 钩子 |
| `GThreadableDevice.h`| 在 `GDevice` 上叠加"渲染线程归属"能力，是 `GLDevice`/`VKDevice` 的直接基类 |
| `GThreadingMode.h`   | 线程模型枚举（`Direct` / `NonThreaded` / `Threaded`） |
| `RenderCommandList.h`| 后端无关的命令录制接口 |
| `IWindow.h`          | 后端无关的窗口抽象（由 `Platform` 实现） |
| `IRenderPass.h`      | 业务 Pass 统一基类 + `ERenderPassEvent`（旧的后端专用 `RendererGL/IRenderPass`、`RendererVK/IVkRenderPass` 已清退删除） |
| `PassScheduler.h`    | 通用 Pass 调度器，仅依赖 `IGDevice / RenderCommandList`，GL/VK 共用 |
| `GDeviceFactory.h`   | 设备创建工厂声明（后端实例化收敛在 `RendererInterface/GDeviceFactory.cpp`） |
| `GpuModel.h` / `GpuMesh.h` | 后端无关的模型/网格 GPU 表示（配合 `AssetLoader` + `AssetGpuUploader`） |
| `Material.h` / `ShaderAsset.h` / `ShaderReflection.h` | 材质 / 着色器资产 / 反射信息 |

## 禁止包含的头文件清单

为了保证抽象层不被穿透，**`RendererCore/` 与所有依赖它的"业务侧"目录**严禁出现以下 include：

| 头文件 | 后端 |
| --- | --- |
| `<vulkan/...>` | Vulkan |
| `<GL/...>`     | OpenGL |
| `<glad/...>`   | OpenGL |
| `<GLFW/glfw3.h>` / `"glfw3.h"` | GLFW |

仓库根目录提供静态扫描脚本：

- [`tools/check_no_backend_headers.bat`](../tools/check_no_backend_headers.bat) （Windows / CMD）
- [`tools/check_no_backend_headers.py`](../tools/check_no_backend_headers.py) （跨平台）

业务示例（如 `Examples/Test_000_UnifiedTriangle`）已在 PreBuild 阶段调用上述脚本，命中即构建失败。

## 目录约束

- 所有 `*.cpp` 同样不允许 include 上述头文件；后端实现请放在 `RendererGL/` 或 `RendererVK/`
  目录下，并通过 `RendererInterface/GDeviceFactory` 暴露的桥接注入。
- 测试用 `Tests/MockDevice.h` 仅用于验证 `IGDevice` 接口是否能被一个空实现完整覆盖，
  不参与生产构建。
