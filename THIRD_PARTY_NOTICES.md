# Third-Party Notices

本项目（以下称"本软件"）在源码或构建过程中使用了下列第三方组件。各组件的版权归其
各自作者所有，并按其各自的许可证条款授权。本文件仅为归属声明（attribution），不构成
对这些组件许可证的修改。

分为两类：

- **随仓库分发**（`Third-Party/`）。
- **本机外部依赖**（Vulkan SDK，由 LunarG 安装器提供，不随本仓库分发）。

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

### GLM — `Third-Party/glm/`
- 许可证：MIT License / The Happy Bunny License
- 版本：0.9.8.3
- 项目主页：https://github.com/g-truc/glm

### GLI — `Third-Party/gli/`
- 许可证：MIT License / The Happy Bunny License
- 版本：0.8.2.0
- 项目主页：https://github.com/g-truc/gli

### stb (stb_image / stb_image_write) — `Third-Party/stb/`
- 许可证：MIT License / Public Domain（双授权，由使用者择一）
- 版权：Sean Barrett
- 项目主页：https://github.com/nothings/stb

### Assimp — `Third-Party/Assimp/`
- 许可证：BSD 3-Clause License
- 项目主页：https://github.com/assimp/assimp

### GLFW — `Third-Party/OpenGL/`
- 许可证：zlib/libpng License
- 项目主页：https://www.glfw.org/

### GLEW — `Third-Party/OpenGL/`
- 许可证：Modified BSD License
- 项目主页：https://glew.sourceforge.net/

---

## 二、本机外部依赖（不随仓库分发）

> Vulkan 头文件与库由本机 [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) 提供（Apache-2.0 等，见 SDK 授权）。
> 安装后环境变量 `VULKAN_SDK` 由安装器写入。

---

## 三、说明

- 本软件自身以 Apache License 2.0 授权，详见根目录 `LICENSE`。
- 若发现任何归属信息有误或缺漏，欢迎提交 issue/PR 更正。
- "Unity" 是 Unity Technologies 的商标，本软件与 Unity Technologies 无任何隶属或背书关系
  （详见 `README.md` 的免责声明）。
