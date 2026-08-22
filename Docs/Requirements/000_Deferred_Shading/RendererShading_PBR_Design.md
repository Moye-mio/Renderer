# RendererShading 新层 + PBR 支持 技术方案

> 状态：**草案，待评审**。本文只描述设计与实施计划，不含已落地代码。
> 相关文档：[`Architecture/00_Overview.md`](../../Architecture/00_Overview.md)（分层与硬约束）、[`UnifiedGfxDevice_Design.md`](UnifiedGfxDevice_Design.md)

---

## 1. 目标与非目标

### 1.1 目标

1. 让项目具备 **物理基础渲染（PBR）** 能力：金属/粗糙度工作流、法线贴图、正确的线性色彩空间、tonemap 输出，以及（可选）IBL 环境光。
2. 新增 **`RendererShading`** 层，承载**着色相关的可复用件**（BRDF、光照、tonemap、IBL、阴影，以及它们所需的 shader 与全屏 Pass 基础设施），消除各 demo 之间已存在的大量重复。命名论证见 §3.1.1，边界见 §3.6。
3. 保持既有硬约束不被破坏：`RendererCore` 继续是纯 RHI 层，依赖方向仍然单向且由 CI 强制。

### 1.2 非目标（本轮明确不做）

- **完整 Render Graph**（自动 barrier 推导、内存 aliasing）。GL/VK 的同步语义差异大，投入产出比在当前阶段不合适。
- **重构 `RendererCore::Material`**。它目前的 `Bind()` 只做了 `BindPipeline`，属性 Apply 完全未实现（`RendererCore/Material.h:60-64`）。补完它需要 UBO 自动 staging + 反射驱动绑定，会显著拉长战线。本轮 PBR 沿用"Pass 内手动绑定资源"的现有模式。
- **场景图 / ECS**。
- **Threaded 线程模式下的验证**。当前 GL/VK 默认均为 `Direct`，Threaded 对 Compute/Descriptor 尚未补齐，本轮不纳入验收。

---

## 2. 现状盘点

### 2.1 已具备的能力（无需额外投入）

| 能力 | 证据 |
|---|---|
| 顶点切线/副切线 | `AssetLoader/AssetTypes.h:85-86`；`ModelLoader.h:16` 的 `calcTangentSpace` 默认开；`AssetGpuUploader.cpp:108-109` 已写入 location 3/4 |
| PBR 材质槽位 | `RendererCore/MaterialInstance.h:26-37` 已有 Normal/Roughness/Metallic/Emissive 槽，且 uploader 已全部上传 |
| Cubemap + Mipmap | `GDescs.h:78-84`（`TextureType::TexCube`、`mipLevels=0` 由后端算、`arrayLayers`）；`AssetGpuUploader.cpp:247-271` 已实现六面上传 |
| HDR 图像加载 | `AssetLoader/ImageLoader.cpp:124`（stb float 路径） |
| Compute Pipeline | `ComputePipelineDesc` + `Dispatch`，ForwardPlus 的 cull CS 已在用 |

**结论**：IBL 与 PBR 都不需要扩 RHI。缺的是语义、算法与输出正确性。

### 2.2 正确性缺陷核查

原列三项，经代码复核后 **(1) 不成立**，实际需修的是 (2)(3)。保留 (1) 的记录以免重复怀疑。

**(1) 三通道纹理的 SRGB 解码 —— 经复核：已正确，无需修改**

初判依据是 `AssetGpuUploader.cpp:66-68` 这段（`GEnums.h:26-59` 的 `Format` 确实没有 `R8G8B8_SRGB`）：

```
// RendererCore::GEnums 当前未提供 R8G8B8_SRGB 三通道 SRGB 格式
case 3: return Format::R8G8B8_UNORM;
case 4: return srgb ? Format::R8G8B8A8_SRGB : Format::R8G8B8A8_UNORM;
```

但追调用链后发现该 `case 3` 对 UNorm8 **不可达**：`PickFormat` 全项目仅有一个调用点（`AssetGpuUploader.cpp:248`），且入参是 `*effectiveImage` —— 而 `:195-244` 的扩通道逻辑已在此之前把 `channels==3` 且 UNorm8/Float32 的图扩成 4 通道，`expanded = image` 的浅复制保留了 `colorSpace`。于是 3 通道 sRGB 贴图实际走的是 `case 4` 的 `R8G8B8A8_SRGB`，解码正确。另外 `ImageLoader.cpp:36-48` 还支持 `opts.desiredChannels`，上层传 4 时 stb 直接输出 4 通道，同样正确。

复核中发现的两个**次要**问题，均不影响 PBR 正确性，本轮不处理：

- `PickFormat` 的压缩纹理分支（`:32-36`）无条件返回 `R8G8B8A8_UNORM`，忽略 `srgb`。当前资产走 TGA 而非 DDS，未触发；注释已说明是"gli format token 不再二次映射"的已知简化。
- 扩通道在 host 侧做全图拷贝，3 通道贴图有 33% 额外内存与一次遍历开销。属性能问题，非正确性问题。

**(2) `sponza_pbr.mtl` 的通道语义被错误映射**

该文件使用 3ds Max 方言，而非标准 `map_Pm`/`map_Pr`/`norm`：`map_Ka` 存 metallic、`map_Ns` 存 roughness、`map_d` 存 opacity mask。而当前映射表走的是 Assimp 通用语义：

```
// AssetLoader/MeshLoader.cpp:26-35
{ aiTextureType_AMBIENT,   TextureSlot::Ambient,   true  },   // ← map_Ka(metallic) 被当 sRGB Ambient
{ aiTextureType_SHININESS, TextureSlot::Roughness, false },   // ← map_Ns→Roughness，恰好正确
{ aiTextureType_OPACITY,   TextureSlot::Metallic,  false }    // ← map_d(mask) 被当 Metallic
```

metallic 贴图既进错了槽位、又被标记为 sRGB（双重错误）；opacity mask 被塞进 Metallic 槽。

**(3) 全项目无 tonemap / gamma 输出**

`ERenderPassEvent` 有 `PostProcess`/`FinalBlit` 槽位但无任何实现。`001_Reflective_shadow_map/Shader/RSMBuffer_FS.glsl:27` 有一行被注释掉的 `pow(2.2)`。PBR 上 HDR 光强与 IBL 后不做 tonemap + sRGB 编码会直接过曝。

### 2.3 与 PBR 无关但更严重的重复（新层的主要动因）

| 重复项 | 规模与证据 |
|---|---|
| 全屏三角形 Pass | `cmd.Draw(3)` + `gl_VertexID` 造顶点模式出现 8+ 处：`DeferredLightingPass.cpp:232`、`ForwardPlusPass.cpp:910`、`ScreenQuadPass.cpp:176`、`WeightedBlendedOITPass.cpp:452` 等；并各自配一份内容近乎相同的 VS（`DeferredLighting_VS.glsl`、`ForwardPlusResolve_VS.glsl`、`WBOIT_Blend_VS.glsl`、`ScreenQuad_VS.glsl`） |
| shader→pipeline 样板 | `ScreenQuadPass.cpp:54-101` 共 47 行"拼路径→读文件→2×ShaderDesc→CreateShader→PipelineDesc→CreatePipeline→缺失兜底"，每个 Pass 一份 |
| 场景类 | `000` 与 `001` 的 `Sponza.h` ≈95% 相同，`Sponza.cpp` 100% 相同，`SponzaGBufferPass.cpp` ≈90% 相同 |
| 光源数据结构 | 已被重写至少三次：旧 demo 的 `LightSources` 类 → `000/TechniqueContext.h` 的 `PointLightDesc`+`FillLightBlock` → `002/SceneDraw.h` 的单方向光 UBO |
| 跨 Pass 资源连接 | `RESOURCE_MANAGER` 字符串键黑板，**拼错键名静默返回 `T{}`**（`TitusGfx.h:247-257`）；`ShadingWithRSMPass.cpp:227-232` 单个 Pass 取 6 个键 |

---

## 3. 架构决策

### 3.1 新增 `RendererShading` 层

```
业务（000 / 001 / 002 / Examples）
        ↓
RendererInterface（唯一对外入口，转发）
        ↓
RendererShading  ← 新增：着色算法及其所需的 shader / 全屏 Pass 基础设施
        ↓
RendererCore（纯 RHI：句柄 / Desc / 命令流 / 线程模型）
        ↓
RendererGL / RendererVK
```

**为什么不放 `RendererCore`**：`Architecture/00_Overview.md:67` 把 Core 职责限定为"定义做什么：`IGDevice` 接口、不透明句柄、资源/管线描述、命令列表、Pass 调度、线程模型"。BRDF、光源、IBL 是渲染算法，放进去会破坏这一层的可替换性与可测试性。

**为什么不放 `RendererInterface`**：该层职责是门面（后端选择、窗口/输入/相机/截图/ImGui）。塞入算法会让它同时承担"入口"和"算法库"两个角色，且它已经是唯一允许同时引用 GL 与 VK 的位置，混入算法会让依赖分析更难。

**命名空间**：`TitusRender`（与 Core 的 `TitusRHI` 区分）。注意目录名与命名空间在本项目中本来就不要求字面一致——`RendererCore`→`TitusRHI`、`RendererGL`→`TitusGraphics`、`RendererVK`→`TitusVkGraphics`，现有约定见 `00_Overview.md:196-201`。

### 3.1.1 命名决策（已定，记录理由以免反复）

定为 **`RendererShading`**。判断标准只有一条：**名字要能替我们拒绝不该进来的东西**。"着色相关"是一句可执行的准入判据——PBR、BRDF、光照、阴影、IBL、tonemap 全部落在其内，而相机、视锥、场景图、资源调度明显落在其外。

被否决的候选：

| 候选 | 否决理由 |
|---|---|
| `RendererCommonModule` | `Common` 语义是"通用的东西"，而任何东西都能被论证为通用 —— 它会直接瓦解 §3.5 的准入规则。`Common`/`Util`/`Misc`/`Shared` 属于同一类反模式：不描述内容，只描述"不知道该放哪"。另：`Module` 后缀在本项目 7 个工程中无一使用，属冗余 |
| `RendererFramework` | "Framework"通常暗示控制反转（框架调用业务代码），而本层主体是被业务调用的库；项目里真正做控制反转的 `PassScheduler` 在 Core。该词也偏大。（补注：Khronos 的 Vulkan-Samples 确实用 `framework/` 放示例共用件，但它同时包含 RHI 抽象，相当于本项目 `RendererCore` + 本层合体，语义并不对应） |
| `RendererPipeline` | 与 RHI 既有概念严重冲突：`PipelineHandle` / `GraphicsPipelineDesc` 已有明确含义 |
| `RendererGraph` | 会被误读为 render graph，而完整 render graph 是本方案的非目标（§1.2） |
| `RendererLib` / `RendererRuntime` | 语义中性、无约束力，无法承担"自动拒绝"的作用 |

**代价与接受**：`Shading` 会把未来的 `Camera` / `Frustum` / `RenderScene` 排除在外（见 §3.6），这是有意为之。`ShaderLibrary` 与 `FullscreenPass` 作为"着色所需的基础设施"纳入本层，理由见 §3.6 末注。

### 3.2 业务侧可见性

现有硬约束是业务只能 include `RendererInterface/*.h`。方案：新增转发头 `RendererInterface/TitusGfxShading.h`，比照现有 `TitusGfxPass.h` 转发 12 个 Core 头的做法转发本层公开头。业务 include 这一个文件即可，"唯一入口"原则不破。

### 3.3 依赖方向与 CI

`Tools/check_deps_direction.bat:28-33` 需新增一行扫描，并调整既有两行：

```bat
REM 新增：RendererShading 不得反向引用 Interface 与任何后端
call :scan "RendererShading" "RendererGL/" "RendererVK/" "RendererInterface/"
REM 调整：Core 追加禁止 include RendererShading
call :scan "RendererCore" "RendererGL/" "RendererVK/" "RendererInterface/" "RendererShading/"
```

`Tools/check_no_backend_headers.py` 的扫描范围需把 `RendererShading/` 纳入（禁止后端 SDK 头）。

### 3.4 工程接入

新建 `RendererShading/RendererShading.vcxproj`（静态库，比照 `RendererCore.vcxproj`；工具集用 `$(DefaultPlatformToolset)`，第三方路径走根 `Directory.Build.props` 已定义的变量）。需要改动：

- `TitusGLRenderer.sln`：新增工程条目 + 配置映射
- `RendererInterface.vcxproj`：新增对 `RendererShading` 的引用
- 各业务 `vcxproj`：新增引用（`000` / `001` / `002` / Examples 按需）

### 3.5 准入规则（防止该层腐化）

这类公共层最常见的失败模式是变成垃圾桶。准入需**同时**满足两条：

1. **语义判据**：它是着色算法，或着色的直接基础设施（§3.6 末注给了唯一两个已论证的例外）。这条由层名本身承担——不属着色的一律拒绝。
2. **重复判据**：已在两个以上 demo 里**真实重复出现**。常规工程用"三次法则"，此处放宽到两次（demo 会持续增加），但不接受"预感以后会用到"。

### 3.6 明确不进本层的内容

**(a) 留在业务侧**（差异本身即研究价值）

| 留在业务 | 理由 |
|---|---|
| 光源的组织与剔除策略 | Forward / Deferred / Forward+ 的核心差异恰在此，下沉会抹掉实验空间 |
| GBuffer 布局 | 各 demo 的设计选择 |
| Pass 编排与技法核心算法 | WBOIT 权重函数、RSM 采样模式、cluster 划分策略——这些是研究对象本身 |

**(b) 属于其他层**（`Shading` 命名的直接后果）

| 内容 | 归属 |
|---|---|
| `Camera` / `Frustum`（view-proj 构造、平面提取、AABB 相交） | 不属着色。候选去处：`Basic`（纯数学）或后续独立的场景层。`000` 的 cluster 剔除目前手搓这部分，需要时单独立项 |
| `RenderScene` / 模型摆放 / AABB 统计（`000`/`001` 的 `Sponza` 类重复） | 同上，属场景语义而非着色 |
| RT 池 / 跨 Pass 资源连接（§4.8） | 属资源调度。若纳入，应放 Core 或独立层，不放本层 |
| shader 文本预处理（§4.1） | 放 `RendererCore`——它是通用文本工具，且 Core 的 `ShaderAsset` 也需要它（Core 不能反向依赖本层） |

**末注：为何 `ShaderLibrary` 与 `FullscreenPass` 可以进本层。** 两者严格说不是着色算法，但都是**着色的直接基础设施**：`ShaderLibrary` 的存在意义就是编译带 `#include` 的着色 shader 并缓存其变体；`FullscreenPass` 的现实用途全部是着色与后处理（tonemap、延迟光照解析、debug 视图）。把它们排除会导致本层无法自洽（BRDF 库没有编译入口、tonemap 没有基类）。这是经过论证的例外，**不作为后续放宽准入的先例**。

---

## 4. 模块设计

### 4.1 Shader 预处理：`#include` + 宏变体（**PBR 的前置阻塞项**）

**现状**：GL 侧 `glShaderSource` 单字符串直接编译；VK 侧 glslang 只注入 `#define VULKAN 100`；两边都无 includer。结果是每个 `.glsl` 手抄 `LAYOUT_BIND` 宏。BRDF 库无法共享。

**设计**：在 Core 新增无状态的纯文本预处理器 `RendererCore/ShaderPreprocessor.h`。它只操作字符串与文件系统，不碰任何后端 SDK 头，不违反 Core 约束；同时 `RendererShading` 与既有 `ShaderAsset` 都能复用。

```cpp
namespace TitusRHI
{
    struct ShaderPreprocessOptions
    {
        std::vector<std::string> includeDirs;
        std::vector<std::pair<std::string, std::string>> defines;
        uint32_t maxIncludeDepth = 16;
    };

    struct ShaderPreprocessResult
    {
        bool                     ok = false;
        std::string              source;        // 展开后的完整源码
        std::vector<std::string> dependencies; // 参与展开的所有文件（供热重载 / 增量）
        std::string              error;
    };

    ShaderPreprocessResult PreprocessShader(const std::string& entryPath,
                                            const ShaderPreprocessOptions& opts);
}
```

实现要点：

1. 递归展开 `#include "..."`，带深度上限与循环包含检测。
2. **必须注入 `#line` 指令**，否则编译错误的行号会指向展开后的位置，调试成本极高。这是最容易被忽略却最痛的细节。
3. `defines` 插入到 `#version` 之后（GLSL 要求 `#version` 必须是首个非注释指令）。
4. 同一文件重复 include 时按 `#pragma once` 语义去重。

**接入点**：`ShaderAsset::LoadAndCreate`（`ShaderAsset.cpp:32-60`）在 `ReadAllBytes` 与 `CreateShader` 之间插入预处理。

**关键约束——VK 侧的源码选择**：`ShaderAssetDesc`（`ShaderAsset.h:22-32`）目前 VK 走 `vkVertexSpvPath` 预编译 SPIR-V，这条路径**吃不到** include 展开。两个选项：

- **选项 A（推荐）**：VK 侧改为优先读 GLSL 源码，交由 glslang 在线编译（`VKDevice::CreateShaderImpl` 已有 magic number 嗅探，GLSL 文本可直接走通）。优点是 GL/VK 共享同一份 `.glsl`，改 shader 无需重新编译资产；缺点是启动时增加 glslang 编译耗时。
- **选项 B**：扩展已有的 `Tools/compile_shader.py`，在构建期做 include 展开 + `-D` 宏 + 预编译为 `.spv`。优点是运行时零开销；缺点是 GL 侧读到的仍是未展开源码，需要两套展开逻辑，且改 shader 要重跑工具。

推荐 A，与现状最契合；若后续启动耗时成为问题，再叠加 B 作为 Release 优化。

**变体策略**：PBR 需要区分"有无某张贴图"。两种做法各有代价：

- 变体宏（`HAS_NORMAL_MAP` 等）：零运行时分支，但 N 张贴图产生 2^N 个 pipeline，Sponza 材质多样会造成变体爆炸。
- 运行时 bitmask（一个 `uint` push constant + shader 内 `if`）：单一 pipeline，代价是少量分支。

**本轮推荐 bitmask**：实现简单、无变体爆炸、便于调试。变体宏机制仍然实现（预处理器支持 `defines`），留给真正必要的场合（如 Opaque 与 WBOIT 两条路径）。

### 4.2 共享 GLSL 库

放在 `RendererShading/Shaders/`，由预处理器的 `includeDirs` 指向：

| 文件 | 内容 | 现状对照 |
|---|---|---|
| `BRDF.glsl` | GGX 分布、Smith 遮蔽、Schlick Fresnel、直接光 `EvaluateBRDF` | 无 |
| `ColorSpace.glsl` | sRGB↔linear 互转 | 无 |
| `Tonemap.glsl` | ACES 近似 / Reinhard | 无 |
| `Lighting.glsl` | 点光/方向光衰减与累加 | 各 demo 各写一份 |
| `Fullscreen.glsl` | `gl_VertexID` 造全屏三角形 | 4 个 VS 各写一份 |
| `Packing.glsl` | 法线编解码、由深度重建视空间位置 | `000`/`001` 各写一份 |
| `PBRMaterial.glsl` | 材质 UBO 布局 + 采样与解包 | 无 |
| `Sampling.glsl` | Hammersley、重要性采样（IBL 与 RSM 共用） | RSM 内联了一份 |

### 4.3 PBR 材质语义

**AssetLoader 侧**：`MeshLoader.cpp:26-35` 的映射表需支持 MTL 方言。设计为显式的方言枚举而非启发式猜测（猜测会误判）：

```cpp
// ModelLoader.h - LoadOptions 新增
enum class MtlDialect { Standard, Max3dsPbr };  // Max3dsPbr: map_Ka=metallic, map_Ns=roughness, map_d=mask
MtlDialect mtlDialect = MtlDialect::Standard;
```

`Max3dsPbr` 下的映射修正：`AMBIENT → Metallic(linear)`、`OPACITY → OpacityMask(linear)`、`SHININESS → Roughness(linear)` 保持。需在 `AssetTypes.h` 的 `TextureSlot` 增加 `OpacityMask` 与 `Occlusion`（并同步 `MaterialInstance.h:26-37`，两者要求逐项对齐）。

**GPU 材质常量**（`RendererShading/PbrMaterial.h`），std140 布局：

```cpp
struct PbrMaterialUBO   // std140, 48 bytes
{
    TitusMath::Vec4 baseColorFactor;   // rgb=baseColor, a=alpha
    TitusMath::Vec4 mrFactors;         // x=metallic y=roughness z=normalScale w=occlusionStrength
    TitusMath::Vec4 emissiveCutoff;    // rgb=emissive, w=alphaCutoff
};
// 贴图存在性通过 push constant 的 uint bitmask 传递（见 4.1 变体策略）
```

**绑定 helper**：比照 `TitusGfxPass.h:105-108` 的 `DrawGpuModelWithDiffuse`，新增 `DrawGpuModelWithPbr`，一次绑定 baseColor / normal / metallic / roughness / occlusion / emissive 六个槽位，沿用现有"连续相同材质跳过 `BindResourceSet`"的优化。该 helper 依赖 `GpuModelHandle`（Interface 自有句柄），因此**放在 Interface 而非 `RendererShading`**。

### 4.4 色彩空间与 Tonemap

**关于色彩空间**：见 §2.2(1) 的复核结论 —— 现有 sRGB 解码链路已正确，**无需改动 `PickFormat`**。

**修正**：新增 `RendererShading/TonemapPass`（继承 4.6 的 `FullscreenPass`），`passEvent = PostProcess`，从 HDR RT 采样 → tonemap → sRGB 编码 → backbuffer。

**HDR RT 格式约束**：`GEnums.h:26-59` 无 `R11G11B10_UFLOAT`。可选 `R16G16B16A16_SFLOAT`（`002/WeightedBlendedOITPass.cpp:68` 已在用）或 `R32G32B32A32_SFLOAT`。**推荐 `R16G16B16A16_SFLOAT`**，带宽是 32F 的一半且精度足够。若后续需要 `R11G11B10`，需在 `GEnums.h` + `GLTranslate.cpp` + VK 翻译表三处同步新增。

### 4.5 光照数据结构

只下沉数据与布局，不下沉组织策略（见 3.6）：

```cpp
// RendererShading/LightTypes.h
struct PointLight       { TitusMath::Vec3 position; float radius; TitusMath::Vec3 color; float intensity; };
struct DirectionalLight { TitusMath::Vec3 direction; float _pad; TitusMath::Vec3 color; float intensity; };

// std140 GPU 镜像 + 世界→视空间填充（替代 000/TechniqueContext.h:62-73 的 FillLightBlock）
struct GpuPointLight    { TitusMath::Vec4 posRadiusVS; TitusMath::Vec4 colorIntensity; };
void FillPointLightBlock(const std::vector<PointLight>& src, const TitusMath::Mat4& view,
                         GpuPointLight* dst, uint32_t capacity, uint32_t& outCount);
```

### 4.6 `FullscreenPass`

```cpp
// RendererShading/FullscreenPass.h
class FullscreenPass : public TitusRHI::IRenderPass
{
public:
    // 子类只提供 FS 路径 + 需要的资源绑定；VS 统一用 Shaders/Fullscreen.glsl
    void Init(TitusRHI::IGDevice& device) override;      // 建 pipeline（含 shader 预处理）
    void Record(TitusRHI::IGDevice&, TitusRHI::RenderCommandList&, uint32_t, uint32_t) override;
protected:
    virtual const char* GetFragmentShaderPath() const = 0;
    virtual void        BindInputs(TitusRHI::RenderCommandList& cmd) {}
    virtual void        ConfigurePipeline(TitusRHI::GraphicsPipelineDesc& pd) {}
};
```

收益：直接消掉 4 个近乎相同的 VS 文件与 4 份 pipeline 样板；`TonemapPass`、后续 blit / debug 视图 / FXAA / SSAO / Bloom 全部是它的特例。

### 4.7 IBL（可选阶段）

三张预计算贴图，RHI 支撑已全部具备（见 2.1）：

| 产物 | 尺寸 / 格式 | 方式 |
|---|---|---|
| Irradiance cube | 32² × 6, `R16G16B16A16_SFLOAT` | 余弦卷积（compute 或 6 次 fullscreen） |
| Prefiltered env cube | 128² × 6, mip 0..4, 同上 | GGX 重要性采样，roughness 随 mip 递增 |
| BRDF LUT | 512², `R16G16_SFLOAT` | 一次性 compute |

预计算 Pass 挂在 `ERenderPassEvent::BeforeRendering` 且只在首帧执行；产物通过 4.8 的资源机制或直接由本层持有。

### 4.8 跨 Pass 资源连接（建议纳入，但可延后）

当前 `RESOURCE_MANAGER` 黑板拼错键名静默返回 `T{}`（`TitusGfx.h:247-257`），且无生命周期、无 resize 重建。**本轮不做完整 render graph**（见 1.2），建议的最小改进：

- 具名 RT 池：按 `(name, desc)` 创建与复用，窗口 resize 时统一重建；
- 强类型 handle 替代字符串键，或至少在 Pass `Init` 阶段做一次"声明的读取项是否都已注册"的启动期校验，把静默失败变成启动期报错。

---

## 5. 分阶段实施计划

每个阶段结束都应是**可编译、可运行、可截图对比**的状态。

### 阶段 0：正确性打底（不含 PBR，独立价值）

- 色彩空间：经复核现有链路已正确，无需改动（见 §2.2(1)）
- 新增 `TonemapPass` + `Tonemap.glsl` / `ColorSpace.glsl`（依赖阶段 1 的 `FullscreenPass`，故此阶段可先内联实现，或与阶段 1 合并）
- **验收**：现有 Blinn-Phong 画面在 GL 与 VK 下均无回归且亮度明显更正确；`000` 三种技法截图对比留档

### 阶段 1：基础设施

- `ShaderPreprocessor`（4.1）+ `#line` 注入 + 循环包含检测
- `ShaderAsset` 接入预处理；确定 VK 源码选项（A/B，见 4.1）
- `FullscreenPass`（4.6）+ `Fullscreen.glsl`；改造 4 个现有全屏 Pass
- 新建 `RendererShading` 工程 + CI 规则 + 转发头（3.3 / 3.4）
- **验收**：4 个 VS 文件被删除且四条全屏路径行为不变；预处理器单测覆盖嵌套 include、循环包含、`#line` 正确性

### 阶段 2：材质语义

- `AssetTypes.h` / `MaterialInstance.h` 增槽位；MTL 方言支持（4.3）
- `PbrMaterialUBO` + `PBRMaterial.glsl`
- `DrawGpuModelWithPbr`（放 Interface）
- **验收**：加载 `Model/SponzaPBR_dds2tga/SponzaPBR.obj`，用 Debug 视图逐通道确认 albedo/normal/metallic/roughness 采样正确（此处强烈建议先做 §5.1 的纹理查看器）

### 阶段 3：直接光 PBR

- `BRDF.glsl` / `Lighting.glsl`
- 先改 `000` 的 **Forward** 路径验证；再扩 Deferred 与 Forward+
- GBuffer 打包（零新增 RT）：`RT0.rgb=albedo, RT0.a=metallic`；`RT1.rgb=normal, RT1.a=roughness`；`RT2.rgb=position, RT2.a=occlusion`
- **验收**：三种技法在相同光照下画面一致（这是本项目现有的核心对比价值）；金属/介电材质表现符合预期

### 阶段 4：IBL（可选）

- 三张预计算贴图 + 环境光项（4.7）
- **验收**：关闭直接光后仅靠环境光的画面合理；金属球的粗糙度序列表现正确

### 后续（不在本轮）

**进本层**：阴影（ShadowMap / CSM / PCF——PBR 之后最缺的一项）、后处理链扩展（FXAA / SSAO / Bloom / TAA，均为 `FullscreenPass` 的特例）、Debug 可视化中与着色相关的部分（通道查看器）。

**不进本层**（见 §3.6(b)，需另行立项）：`Camera` / `Frustum`、共享 `RenderScene`、RT 池与资源调度。

### 5.1 关于 Debug 可视化的前置建议

阶段 2 的验收几乎无法靠肉眼完成——需要单独查看 metallic/roughness 通道。建议在阶段 2 之前先做一个最小的纹理查看器（选任意 RT/贴图显示，支持通道隔离与数值范围/gamma 切换），它本身就是 `FullscreenPass` 的一个特例，成本很低但会显著降低后续所有阶段的调试成本。

---

## 6. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| VK 侧 GLSL 在线编译拖慢启动 | 中 | 先测量；必要时叠加构建期预编译（4.1 选项 B），或加 SPIR-V 磁盘缓存 |
| GL 侧多 sampler 绑定依赖反射，现有路径只验证过单张 diffuse | **高** | 阶段 2 先用最小场景验证 6 个 sampler 的 set/binding 映射；`ShaderReflector` 的 GLSL 正则回退路径需重点检查 |
| MTL 方言判定 | 中 | 用显式枚举而非启发式猜测；`Model/` 内资产逐个标注 |
| 变体/bitmask 选择反复 | 低 | 预处理器同时支持两者，本轮先 bitmask |
| `RendererShading` 腐化为垃圾桶 | 中（因命名收窄而降低） | §3.5 的双判据（语义 + 两次重复）+ §3.6 的显式排除清单，评审时逐项对照。层名"Shading"本身即第一道拒绝机制 |
| 三个 demo 同步改动量 | 中 | 阶段 3 只改 `000`；`001`/`002` 在其后单独处理。`002` 的 WBOIT 路径按惯例只算 diffuse + 简化 spec，不接完整 BRDF |
| GL/VK 画面不一致 | 中 | 每阶段双后端截图对比纳入验收；沿用现有 `--screenshot-at` 机制 |

---

## 7. 待决策清单（评审时确认）

**已决策**

- ✅ **新建独立工程**（而非先在 `000` 内验证后抽取）。理由：建工程的一次性成本（sln + 2 个 CI 脚本 + vcxproj 引用）低于后续搬迁 include 路径的成本，且早建能让 §3.5 的准入规则从第一天生效。
- ✅ **层名 `RendererShading`**，命名空间 `TitusRender`。论证见 §3.1.1。

**仍待确认**

1. **VK shader 源码路径**：选项 A（GLSL 在线编译，推荐）还是 B（构建期预编译）？这一项影响 §4.1 的实现形态，需在阶段 1 开工前定。
2. **本轮范围**：到阶段 3（直接光）还是含阶段 4（IBL）？
3. `000`/`001` 的重复场景类与 GBuffer Pass（≈90-95% 重复）如何处理？注意按 §3.6(b)，`RenderScene` **不进本层**，所以这是一个独立问题：留在业务、放 `Basic`、还是后续另立场景层。
4. §4.8 的资源连接改进是否纳入本轮？（按 §3.6(b) 它不进本层，若纳入需先定归属）
5. 未纳入 git 跟踪的旧 demo（`000_Deferred_Shading/`、`002_Non-interleaved_Deferred_Shading_of_Interleaved_Sample/`）：迁移到新架构、还是删除？它们使用旧 API（`TitusGraphics::RESOURCE_MANAGER`、`shared_ptr<Texture>`），容易在检索时被误认为现役代码。

---

## 8. 改动文件汇总（预估）

**新增**

```
RendererShading/RendererShading.vcxproj (+ .filters)
RendererShading/ShaderLibrary.h/.cpp          // 变体缓存：(path, defines) → ShaderHandle
RendererShading/FullscreenPass.h/.cpp
RendererShading/TonemapPass.h/.cpp
RendererShading/PbrMaterial.h
RendererShading/LightTypes.h
RendererShading/IblPrecompute.h/.cpp          // 阶段 4
RendererShading/Shaders/*.glsl                // 见 4.2
RendererCore/ShaderPreprocessor.h/.cpp
RendererInterface/TitusGfxShading.h             // 转发头
```

**修改**

```
RendererCore/ShaderAsset.cpp        // 接入预处理（:32-60 之间）
RendererCore/MaterialInstance.h     // 增 OpacityMask / Occlusion 槽（:26-37）
AssetLoader/AssetTypes.h            // TextureSlot 同步
AssetLoader/MeshLoader.cpp          // MTL 方言映射（:26-35）
AssetLoader/ModelLoader.h           // LoadOptions 增 mtlDialect
RendererInterface/TitusGfxPass.h    // 增 DrawGpuModelWithPbr
Tools/check_deps_direction.bat      // 增 RendererShading 扫描（:28-33）
Tools/check_no_backend_headers.py   // 扫描范围纳入 RendererShading
TitusGLRenderer.sln                 // 新工程 + 配置映射
各业务 vcxproj                       // 工程引用
000_Forward_Deferred_ForwardPlus/*  // 阶段 3 主战场
```

**删除**（阶段 1）

```
000_Forward_Deferred_ForwardPlus/Shader/DeferredLighting_VS.glsl
000_Forward_Deferred_ForwardPlus/Shader/ForwardPlusResolve_VS.glsl
001_Reflective_shadow_map/Shader/ScreenQuad_VS.glsl
002_Order_Independent_Transparency/Shader/WBOIT_Blend_VS.glsl
```
