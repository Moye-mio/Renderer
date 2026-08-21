# 003_Toon_Shading

卡通渲染 / NPR 对比示例。场景是妮露角色（`Model/Nilou/`），不用 Sponza。

当前里程碑是 **M1：Diffuse 出图**。Cel-Ramp、描边、脸 SDF 见 [`Docs/003_Toon_Shading_Design.md`](../Docs/003_Toon_Shading_Design.md) 与 [`Docs/003_Toon_Shading_Tasks.md`](../Docs/003_Toon_Shading_Tasks.md)。

业务侧仅依赖 `RendererInterface`（命名空间 **`TitusRHI`**）。源资产是 FBX + PNG；本仓 Assimp 无法安全读取该二进制 FBX，因此运行时加载 `Model/Nilou/Nilou.obj`（`Tools/export_nilou_obj.py` 用 ufbx 从 FBX 烤出的 T-pose）。NPR 贴图按材质名查表绑定，再 `APP::UploadGpuModel`。**GL / VK 双后端**可运行。

## 渲染流程（M1）

```
ToonPass    (OpaqueShading)    采样 Diffuse + 方向光，画到 backbuffer
```

## 构建 / 运行

```bash
003_Toon_Shading\Build\build_debug.bat
003_Toon_Shading\Build\run_debug.bat
003_Toon_Shading\Build\run_debug.bat --backend=vk
003_Toon_Shading\Build\run_debug.bat --backend=gl --screenshot-at=2 --screenshot-dir=003_Toon_Shading/results
```

```bash
003_Toon_Shading.exe                  # 默认 OpenGL
003_Toon_Shading.exe --backend=gl
003_Toon_Shading.exe --backend=vk
003_Toon_Shading.exe --backend=gl --screenshot-at=2 --screenshot-dir=003_Toon_Shading/results
003_Toon_Shading.exe --backend=vk --screenshot-at=2 --screenshot-dir=003_Toon_Shading/results
```

内置飞行相机：WASD / QE 平移，右键拖拽旋转。ImGui「Toon Shading」可调主光 yaw / pitch 与环境光。

从 FBX 重新导出 OBJ（改源网格后）：

```bash
pip install ufbx
python Tools/export_nilou_obj.py
```

## 模块依赖

```
003_Toon_Shading
   ├─ RendererInterface（TitusRHI::APP / IRenderPass / CAMERA / IMGUI …）
   ├─ AssetLoader（TitusAsset：Nilou CPU IR + PNG）
   └─ Basic
```

Pass 继承 `TitusRHI::IRenderPass`；不直接 include `RendererGL/` / `RendererVK/` / `RendererCore/`。
