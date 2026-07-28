# AssetLoader

> 任务 11 / M5-A：纯 CPU 资源加载层

## 模块目标

把"磁盘 IO + 第三方解码（Assimp / stb_image / gli）"统一收拢到一个静态库，
彻底与渲染后端解耦：

- **零 GPU 依赖**：不 include 任何 `gl/` / `vulkan/` / `RendererCore/` 头文件
- **纯 CPU 输出**：仅产出 `ImageAssetData / MeshAssetData / ModelAssetData`
- **后端无关**：GL / VK / Null 都可消费同一份 IR
- **可单元测试**：纯 CPU 流程，无窗口 / 无设备依赖

## 模块结构

```
AssetLoader/
├── AssetTypes.h       // CPU IR：Image/Mesh/Model 数据结构（命名空间 TitusAsset）
├── FileSystem.h/.cpp  // 通用 IO 工具：ReadAllBytes / GetExtensionLower 等
├── ImageLoader.h/.cpp // PNG/JPG/HDR/DDS 解码（stb_image + gli）
├── MeshLoader.h/.cpp  // aiMesh / aiMaterial → MeshAssetData
├── ModelLoader.h/.cpp // 顶层入口：磁盘文件 → ModelAssetData
└── AssetLoader.h/.cpp // 模块统一 include + 版本号符号
```

## 依赖方向

```
[业务模块] ──► RendererInterface ──► RendererCore ──► RendererVK / Renderer
                                                ▲
                                                │ (按需)
[业务模块] ──► AssetLoader (纯 CPU) ◄────────────┘
```

`AssetLoader` 是**纯叶子库**：

- 它**不依赖**任何项目模块（除 Basic 的工具头）
- 任何模块都**可以依赖**它

## 业务侧用法

```cpp
#include "AssetLoader/AssetLoader.h"

TitusAsset::ModelAssetData model;
TitusAsset::ModelLoadOptions opts;
opts.loadTextures = true;

if (!TitusAsset::LoadModel("Model/Sponza/sponza.obj", model, opts))
    return -1;

for (const auto& mesh : model.meshes)
{
    // mesh.vertices / mesh.indices / mesh.material
    // 把这些数据交给 RendererInterface 创建 GPU 资源
}
for (const auto& img : model.sharedImages)
{
    // img->mips[0].pixels.bytes → CreateTexture(...)
}
```

## 三方依赖

继承自 Renderer 工程的环境变量：

- `$(STB_IMAGE)`：stb_image 头文件
- `$(ASSIMP)`：Assimp 头文件 + lib（链接由消费方负责）
- `$(GLI)`：gli 头文件
- `$(GLM)`：glm 数学库

注意：`stb_image.h` 的 `STB_IMAGE_IMPLEMENTATION` 由 AssetLoader 自身在
`ImageLoader.cpp` 中定义（全仓唯一定义处），不再依赖任何渲染后端工程提供。

## CI 静态扫描

PreBuildEvent 会调用 `tools\check_deps_direction.bat`，确保 AssetLoader 不会
反向 include 任何渲染后端头文件。
