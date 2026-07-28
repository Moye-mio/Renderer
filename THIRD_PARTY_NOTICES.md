# Third-Party Notices

本项目（以下称"本软件"）在源码或构建过程中使用了下列第三方组件。各组件的版权归其
各自作者所有，并按其各自的许可证条款授权。本文件仅为归属声明（attribution），不构成
对这些组件许可证的修改。

分为两类：

- **随仓库分发**（位于 `Third-Party/`，会随本仓库一并公开）。
- **外部依赖**（位于 `GraphicSDK/`，已被 `.gitignore` 排除、不随仓库分发，需使用者自行获取；
  在此声明是因为本软件在编译/链接时会使用它们）。

---

## 一、随仓库分发的组件（`Third-Party/`）

### Dear ImGui — `Third-Party/imgui_master/`
- 许可证：MIT License
- 版权：Copyright (c) 2014-2018 Omar Cornut
- 项目主页：https://github.com/ocornut/imgui

### IconFontCppHeaders — `Third-Party/iconfontheaders/`
- 许可证：MIT License（此仓库副本随附 `LICENSE`）
- 版权：Copyright (c) 2015 Juliette Foucaut
- 项目主页：https://github.com/juliettef/IconFontCppHeaders

### GLFW（随 Dear ImGui 示例附带）— `Third-Party/imgui_master/examples/libs/glfw/`
- 许可证：zlib/libpng License
- 版权：Copyright (c) 2002-2006 Marcus Geelnard；Copyright (c) 2006-2019 Camilla Löwy 等
- 项目主页：https://www.glfw.org/

---

## 二、外部依赖（`GraphicSDK/`，不随仓库分发）

> 下列组件不包含在本仓库中。使用者需自行下载并放置到 `GraphicSDK/` 下对应目录后方可构建。
> 各组件许可证以其官方发布为准，此处仅为概述与归属。

| 组件 | 目录 | 许可证（概述） | 主页 |
|---|---|---|---|
| Eigen | `GraphicSDK/Eigen/` | MPL-2.0（核心）；部分模块为 LGPL/BSD/MINPACK 等，详见其 `COPYING.*` | https://eigen.tuxfamily.org/ |
| OpenCV | `GraphicSDK/opencv/` | Apache-2.0（本体）；捆绑第三方含 FFmpeg (LGPL-2.1)、libpng、libjpeg-turbo、OpenEXR、protobuf、ittnotify(BSD/GPL 双许可) 等，详见其 `LICENSE` 与 `etc/licenses/` | https://opencv.org/ |
| GLM | `GraphicSDK/glm/` | MIT License / The Happy Bunny License | https://github.com/g-truc/glm |
| GLI | `GraphicSDK/gli/` | MIT License | https://github.com/g-truc/gli |
| stb (stb_image 等) | `GraphicSDK/stb_image/` | MIT License / Public Domain（双授权，由使用者择一） | https://github.com/nothings/stb |
| Assimp | `GraphicSDK/assimp/` | BSD 3-Clause License | https://github.com/assimp/assimp |
| GLFW | `GraphicSDK/glfw/` | zlib/libpng License | https://www.glfw.org/ |
| glad | `GraphicSDK/glad/` | MIT License（生成的加载器）；所载 OpenGL/Khronos 规范为 Apache-2.0 | https://github.com/Dav1dde/glad |
| Boost | `GraphicSDK/boost/` | Boost Software License 1.0 | https://www.boost.org/ |
| Dear ImGui | `GraphicSDK/imgui/` | MIT License | https://github.com/ocornut/imgui |
| OpenXR SDK | `GraphicSDK/openxr/` | Apache-2.0 | https://github.com/KhronosGroup/OpenXR-SDK |
| Vulkan Headers / SDK | `GraphicSDK/vulkan/` | Apache-2.0（Vulkan-Headers）；Vulkan SDK 各组件见 LunarG 授权 | https://www.lunarg.com/vulkan-sdk/ |

> 关于 GPL/LGPL 组件的提示：OpenCV 在部分构建配置下会捆绑受 LGPL-2.1 约束的 FFmpeg，
> 以及以 BSD/GPL 双许可发布的 ittnotify。如需以静态链接或再分发形式使用这些组件，请先核对
> 相应许可证的义务（例如 LGPL 对再链接、GPL 对传染性的要求）。若仅使用 OpenCV 的动态库
> 且不再分发，通常无额外义务，但仍以官方许可证为准。

---

## 三、说明

- 本软件自身以 Apache License 2.0 授权，详见根目录 `LICENSE`。
- 若发现任何归属信息有误或缺漏，欢迎提交 issue/PR 更正。
- "Unity" 是 Unity Technologies 的商标，本软件与 Unity Technologies 无任何隶属或背书关系
  （详见 `README.md` 的免责声明）。
