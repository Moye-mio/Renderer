# AssetLoader

纯 CPU 资源加载层：把磁盘 IO + 第三方解码（Assimp / stb_image / gli）收拢到静态库，
与渲染后端解耦。

## 模块目标

- **零 GPU 依赖**：不 include 任何 `gl/` / `vulkan/` / `RendererCore/` / `RendererGL/` / `RendererVK/` 头
- **纯 CPU 输出**：仅产出 `ImageAssetData` / `MeshAssetData` / `ModelAssetData`
- **后端无关**：GL / VK / Null 都可消费同一份 IR
- **可单元测试**：无窗口 / 无设备依赖
- **截图写出**：`ImageWriter` 提供 `SaveImage2DPNG`（RGBA8 → PNG），供 `APP::CaptureScreenshot` 使用

## 模块结构

```
AssetLoader/
├── AssetTypes.h       // CPU IR：Image/Mesh/Model（命名空间 TitusAsset）
├── FileSystem.h/.cpp  // 通用 IO：ReadAllBytes / GetExtensionLower 等
├── ImageLoader.h/.cpp // PNG/JPG/HDR/DDS 解码（stb_image + gli）
├── ImageWriter.h/.cpp // RGBA8 → PNG（stb_image_write）
├── MeshLoader.h/.cpp  // aiMesh / aiMaterial → MeshAssetData
├── ModelLoader.h/.cpp // 顶层入口：磁盘文件 → ModelAssetData
└── AssetLoader.h/.cpp // 模块统一 include + 版本号符号
```

## 依赖方向

```
[业务模块] ──► RendererInterface ──► RendererCore ──► RendererGL / RendererVK
                                                ▲
                                                │ (按需)
[业务模块] ──► AssetLoader (纯 CPU) ◄────────────┘
```

`AssetLoader` 是**纯叶子库**：不依赖渲染模块（除 Basic 工具头）；任何模块都可依赖它。

## 业务侧用法

```cpp
#include "AssetLoader/AssetLoader.h"

TitusAsset::ModelAssetData model;
TitusAsset::ModelLoadOptions opts;
opts.loadTextures = true;

if (!TitusAsset::LoadModel("Model/Sponza/sponza.obj", model, opts))
    return -1;

// 交给 RendererInterface：APP::UploadGpuModel(model) 等
for (const auto& mesh : model.meshes)
{
    // mesh.vertices / mesh.indices / mesh.material
}
for (const auto& img : model.sharedImages)
{
    // img->mips[0].pixels.bytes → CreateTexture(...)
}

// 截图写盘（通常由门面调用，业务也可直接用）
TitusAsset::SaveImage2DPNG("shot.png", width, height, rgba8);
```

## 三方依赖

继承自 GraphicSDK 环境变量：

- `$(STB_IMAGE)`：stb_image / stb_image_write
- `$(ASSIMP)`：Assimp
- `$(GLI)`：gli
- `$(GLM)`：glm

`STB_IMAGE_IMPLEMENTATION` / `STB_IMAGE_WRITE_IMPLEMENTATION` 由 AssetLoader 自身在
对应 `.cpp` 中唯一定义，不再依赖渲染后端工程。

## CI 静态扫描

PreBuildEvent 调用 `tools\check_deps_direction.bat`，确保不会反向 include 渲染后端头。
