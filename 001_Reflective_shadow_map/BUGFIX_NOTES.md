# 001_Reflective_shadow_map 改造期 Bug 记录

> 改造背景：将 001 项目从直接依赖 `Renderer/`（旧 GL 直调路径）切换到通过 `RendererInterface / IGDevice` 抽象层访问 GPU。
> 切换后画面异常，通过 RenderDoc MCP 抓帧定位到以下两个 bug。

---

## Bug 1：Compute Pass 的 6 张输入纹理全部"看到"同一张图

### 现象
- RenderDoc 抓帧（`Rdcs/001_3.rdc`）显示 `ShadingWithRSM_CS` 的 6 个 SRV
  （`u_AlbedoTexture` ~ `u_RSMPositionTexture`）全部指向 `ResourceId::868`
  （即 GBuffer Albedo），而 CPU 侧 `BindResourceSet` 明明把 6 张不同的纹理
  分别绑到了 unit 0..5。
- 直接表现为：屏幕上出现的 RSM 间接光颜色完全错误。

### 根因
GL 后端在 `CreatePipelineImpl` 中**只反射了 UniformBuffer**
（`glUniformBlockBinding`），完全没处理 sampler / storage image 的 unit 映射。

而 `Shader/ShadingWithRSM_CS.glsl` 里的 6 张 sampler 没有写 `layout(binding=N)`：

```glsl
uniform sampler2D u_AlbedoTexture;     // 默认 location/unit 都是 0
uniform sampler2D u_NormalTexture;     // 默认 location/unit 都是 0
...
```

按 GL 规范，**没有 `glUniform1i(loc, unit)` 显式映射的 sampler 默认全部从 unit 0 采样**。
即便 `GLCommandList::BindResourceSet` 把 6 张纹理依次绑到 unit 0..5，shader 内部
仍然全部从 unit 0 取，自然全是同一张图。

雪上加霜的是，`ShadingWithRSMPass::Init` 当时也只把 2 个 UBO 写进
`cpd.resourceBindings`，6 张 sampler + 1 张 storage image 完全没声明，即便
GL 后端做反射也找不到 name。

### 修复（双侧改动）

**1) `Renderer/GLDevice.cpp`**：Graphics 与 Compute 两个 `CreatePipelineImpl`
在 link 成功后增加 sampler / storage image 反射映射。伪码：

```cpp
glUseProgram(pe.program);
for (const auto& rb : desc.resourceBindings)
{
    switch (rb.type)
    {
    case ResourceBindingType::UniformBuffer:
        glUniformBlockBinding(pe.program,
            glGetUniformBlockIndex(pe.program, rb.name.c_str()),
            rb.binding);
        break;
    case ResourceBindingType::SampledTexture:
    case ResourceBindingType::CombinedImageSampler:
    case ResourceBindingType::StorageTexture:
        glUniform1i(
            glGetUniformLocation(pe.program, rb.name.c_str()),
            static_cast<GLint>(rb.binding));   // ★ 关键
        break;
    }
}
glUseProgram(0);
```

**2) `001_Reflective_shadow_map/ShadingWithRSMPass.cpp`**：
补全 6 张 sampler + 1 张 storage image 的 `ResourceBinding` 声明，让 GL 反射阶段拿得到 name：

```cpp
auto addSampler = [&](const char* name, uint32_t binding) {
    ResourceBinding rb{};
    rb.name    = name;
    rb.set     = 0;
    rb.binding = binding;
    rb.type    = ResourceBindingType::CombinedImageSampler;
    rb.stages  = ShaderStage::Compute;
    cpd.resourceBindings.push_back(rb);
};
addSampler("u_AlbedoTexture",      0);
addSampler("u_NormalTexture",      1);
addSampler("u_PositionTexture",    2);
addSampler("u_RSMFluxTexture",     3);
addSampler("u_RSMNormalTexture",   4);
addSampler("u_RSMPositionTexture", 5);

ResourceBinding outImg{};
outImg.name    = "u_OutputImage";
outImg.binding = 0;
outImg.type    = ResourceBindingType::StorageTexture;
outImg.stages  = ShaderStage::Compute;
cpd.resourceBindings.push_back(outImg);
```

### 验证
重新抓帧，Compute Pass 的 6 个 SRV 各自指向 GBuffer Albedo / Normal /
Position / RSM Flux / RSM Normal / RSM Position 这 6 张不同纹理，`u_OutputImage`
正确指向 `ShadingWithRSMPass.OutputImage`。

---

## Bug 2：GBuffer Pass 部分 fragment 采样到接近黑色

### 现象
- 同一份 `Model/sponza/sponza.obj` + 同一组 diffuse 纹理：
  - **改造前**（旧 GLUtils + Renderer/Model.cpp 直调路径）画面正常；
  - **改造后**（AssetLoader + UploadGpuModel 路径）部分 mesh 表面看起来"接近黑色"。
- 用 RenderDoc 单独读取相关 diffuse texture 的 mip 0 中心像素值
  （如 `ResourceId::62` 的 (800, 600) = (0.47, 0.40, 0.28)）：
  贴图本身**完全正常**，只是被采到了"应该不该被采到的 UV 位置"（顶部黑边/过渡区）。

### 根因：UV V 轴翻转方向不一致

| 路径 | `aiProcess_FlipUVs` | `stbi_set_flip_vertically_on_load` | 等效 V 翻转次数 |
|---|---|---|---|
| **旧（`Renderer/Model.cpp` L64）** | ❌ 不开 | ✓ 开 | **1 次（OK）** |
| **新（`AssetLoader/ModelLoader.cpp` 默认 Options）** | ✓ 默认开 | ✓ 默认开 | **2 次 = 等效不翻转** |

约定：
- OpenGL UV：V 朝上；
- stb_image / 大多数图像格式：内存中第 0 行是图像顶部，等价于 V 朝下；
- 让两者对齐，**恰好需要 1 次 V 翻转**。

新路径多了一次 UV 翻转（assimp 端 `aiProcess_FlipUVs`），相当于把 fragment shader 中的
V 坐标变成了 `1 - V`：
- 原本采到纹理 `(u, 0.7)` 的 fragment，现在采到 `(u, 0.3)`；
- sponza diffuse 纹理顶部往往有黑边/无效像素，错位后大量 fragment 就会"采到接近黑色的值"。

### 修复
在 `001_Reflective_shadow_map/main.cpp` 调用 `LoadModel` 时显式关掉
`flipUVs`，与旧路径行为对齐：

```cpp
::TitusAsset::ModelAssetData modelAsset{};
::TitusAsset::ModelLoadOptions modelOpts{};
modelOpts.flipUVs = false;          // ★ 与旧路径 Renderer/Model.cpp 对齐
const std::string sponzaPath = std::string(SOLUTION_DIR) + "Model/sponza/sponza.obj";
if (!::TitusAsset::LoadModel(sponzaPath, modelAsset, modelOpts))
{
    std::cerr << "[001] failed to load Sponza model\n";
    APP::ShutdownApp();
    return 1;
}
```

### 备注（关于 sRGB）
顺带发现的、但**不是当前现象的根因**：
- 旧路径：3 通道 sRGB diffuse → `GL_SRGB`，驱动自动 sRGB→Linear；
- 新路径：`AssetGpuUploader::PickFormat` 把 3 通道 sRGB 退化成 `R8G8B8_UNORM`
  （`RendererCore::GEnums` 暂未提供 `R8G8B8_SRGB`），不会自动做 gamma 解码；
  ScreenQuad FS 又做了一次 `pow(c, 1/2.2)` 输出，"恰好近似抵消"，所以视觉上
  色调差异不大，只是中间运算（直接光/间接光的乘法）会带轻微 gamma 误差。

后续若发现整体偏色，再补 `R8G8B8_SRGB` 三通道枚举或在 fs 内手动 `pow(albedo, 2.2)` 解码。

### 验证
重新编译并抓帧：GBuffer Pass 的 Albedo (RT0) 输出可见 sponza 真实墙面颜色
（米黄/灰白），不再是大面积黑斑。

---

## 共同教训：抽象层封装时易丢失的"隐性约定"

| 维度 | Bug 1 | Bug 2 |
|---|---|---|
| 丢失的约定 | sampler uniform 必须 `glUniform1i` 显式映射到 texture unit | OBJ 的 UV 是 OpenGL 风格（V 朝上），全链路只该有 1 次 V 翻转 |
| 旧路径（直接 GL 调用）的处理 | `ShaderProgram::SetTextureUniform` 内部主动 `glUniform1i` | `Renderer/Model.cpp` assimp 默认不带 `aiProcess_FlipUVs` |
| 新路径（IGDevice / AssetLoader 抽象层）的处理 | 仅反射 UBO，sampler / image 一律没处理 | `ModelLoadOptions::flipUVs` 默认 `true`，与旧 Model.cpp 不一致 |
| 后果 | 多 sampler shader 全部采 unit 0 → 串图 | UV 上下颠倒 → 采到纹理黑边 |

教训：
1. **抽象层接口默认值要与既有路径行为对齐**（如 `flipUVs` 默认值）；
   否则同一份资产、同一份代码，仅切后端就出现行为偏移，排查成本极高。
2. **后端提交资源前要做完整的反射/绑定**，不能只覆盖 UBO 一种类型；
   GL 在没有显式 `layout(binding=...)` 时，sampler 默认 unit=0 是个**沉默的陷阱**——
   不会报错、不会黑屏，只是"全部采同一张图"。
3. 抽象层（GLDevice/VKDevice/AssetGpuUploader）一旦默认值定下来，就该在
   `ResourceBinding` / `LoadOptions` 这种上层结构中显式声明，而不是依赖各后端各自补默认。
