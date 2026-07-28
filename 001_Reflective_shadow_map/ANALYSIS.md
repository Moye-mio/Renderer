# 001_Reflective_shadow_map 工程深度逻辑分析

## 一、功能定位与适用场景

### 1.1 功能定位

本项目实现 **Reflective Shadow Map (RSM)** 技术，是一种基于图像的全局光照(Global Illumination)解决方案。RSM 通过将光源视角的像素视为虚拟点光源(VPL, Virtual Point Light)，实现实时间接光照效果。

### 1.2 适用场景

| 场景特征 | 适用性 |
|---------|--------|
| 室内场景渲染 | ✅ 高度适用（如 Cornell Box、Sponza 模型） |
| 实时交互应用 | ✅ 适用（帧率可接受） |
| 单一方向光照明 | ✅ 当前实现仅支持单一方向光 |
| 动态光源变化 | ⚠️ 需要重新生成 RSM，有性能开销 |

### 1.3 技术特点

- **延迟渲染架构**：采用 G-Buffer 存储几何信息
- **Compute Shader 加速**：使用 GPU 计算着色器进行间接光照计算
- **蒙特卡洛采样**：通过随机采样 VPL 近似间接光照积分

---

## 二、执行流程分析

### 2.1 整体流程概览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              main() 入口                                 │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         配置阶段 (Configuration)                         │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐       │
│  │ 窗口尺寸 1920x1152│  │ 禁用光标          │  │ 禁用GUI          │       │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘       │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        注册阶段 (Registration)                           │
│  ┌──────────────────┐  ┌──────────────────────────────────────────┐     │
│  │ GameObject       │  │ RenderPass (按 executionOrder 排序)      │     │
│  │ HalfCornellBox   │  │ ┌──────────────────────────────────────┐ │     │
│  └──────────────────┘  │ │ 1. HalfCornellBoxGBufferPass        │ │     │
│                        │ │ 2. RSMBufferPass                    │ │     │
│                        │ │ 3. ShadingWithRSMPass               │ │     │
│                        │ │ 4. ScreenQuadPass                   │ │     │
│                        │ └──────────────────────────────────────┘ │     │
│                        └──────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         初始化阶段 (InitApp)                             │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                    各 Pass 按顺序执行 InitV()                       │ │
│  │  ┌──────────────────┐                                              │ │
│  │  │ GBufferPass.Init │ 创建 G-Buffer 纹理 (Albedo/Normal/Position)  │ │
│  │  └──────────────────┘                                              │ │
│  │  ┌──────────────────┐                                              │ │
│  │  │ RSMBufferPass    │ 创建 RSM 纹理 (Flux/Normal/Position)         │ │
│  │  │ .Init            │ 计算光源 VP 矩阵                              │ │
│  │  └──────────────────┘                                              │ │
│  │  ┌──────────────────┐                                              │ │
│  │  │ ShadingPass.Init │ 创建输出纹理、初始化 VPL 采样点              │ │
│  │  └──────────────────┘                                              │ │
│  │  ┌──────────────────┐                                              │ │
│  │  │ ScreenQuad.Init  │ 绑定最终输出纹理                             │ │
│  │  └──────────────────┘                                              │ │
│  └────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         渲染循环 (UpdateApp)                             │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                    各 Pass 按顺序执行 UpdateV()                     │ │
│  │                                                                     │ │
│  │   ┌─────────────────────────────────────────────────────────────┐   │ │
│  │   │ Pass 1: HalfCornellBoxGBufferPass                           │   │ │
│  │   │ ┌─────────────────────────────────────────────────────────┐ │   │ │
│  │   │ │ 绑定 FBO → 清除缓冲 → 启用深度测试/背面剔除             │ │   │ │
│  │   │ │ → 渲染场景到 G-Buffer (Albedo/Normal/Position/Depth)    │ │   │ │
│  │   │ └─────────────────────────────────────────────────────────┘ │   │ │
│  │   └─────────────────────────────────────────────────────────────┘   │ │
│  │                           │                                         │ │
│  │                           ▼                                         │ │
│  │   ┌─────────────────────────────────────────────────────────────┐   │ │
│  │   │ Pass 2: RSMBufferPass                                       │   │ │
│  │   │ ┌─────────────────────────────────────────────────────────┐ │   │ │
│  │   │ │ 绑定 RSM FBO → 设置视口为 256x256                        │ │   │ │
│  │   │ │ → 从光源视角渲染场景 → 输出 Flux/Normal/Position        │ │   │ │
│  │   │ │ → 恢复视口尺寸                                          │ │   │ │
│  │   │ └─────────────────────────────────────────────────────────┘ │   │ │
│  │   └─────────────────────────────────────────────────────────────┘   │ │
│  │                           │                                         │ │
│  │                           ▼                                         │ │
│  │   ┌─────────────────────────────────────────────────────────────┐   │ │
│  │   │ Pass 3: ShadingWithRSMPass                                  │   │ │
│  │   │ ┌─────────────────────────────────────────────────────────┐ │   │ │
│  │   │ │ Compute Shader:                                          │ │   │ │
│  │   │ │ • 计算直接光照 (方向光)                                  │ │   │ │
│  │   │ │ • 采样 32 个 VPL 计算间接光照                            │ │   │ │
│  │   │ │ • 输出到 ShadingTexture                                  │ │   │ │
│  │   │ └─────────────────────────────────────────────────────────┘ │   │ │
│  │   └─────────────────────────────────────────────────────────────┘   │ │
│  │                           │                                         │ │
│  │                           ▼                                         │ │
│  │   ┌─────────────────────────────────────────────────────────────┐   │ │
│  │   │ Pass 4: ScreenQuadPass                                      │   │ │
│  │   │ ┌─────────────────────────────────────────────────────────┐ │   │ │
│  │   │ │ 绑定默认 FBO (屏幕)                                      │ │   │ │
│  │   │ │ → 渲染全屏四边形 → Gamma 校正 → 输出到屏幕              │ │   │ │
│  │   │ └─────────────────────────────────────────────────────────┘ │   │ │
│  │   └─────────────────────────────────────────────────────────────┘   │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                         │
│                         └─────── 循环 ───────→                          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                              程序退出                                    │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 关键流程节点详解

#### 2.2.1 G-Buffer 生成阶段 (HalfCornellBoxGBufferPass)

```
┌──────────────────────────────────────────────────────────────────┐
│                    G-Buffer 生成流程                              │
├──────────────────────────────────────────────────────────────────┤
│  输入: 场景几何 (Sponza 模型)                                     │
│  输出: 4 张纹理 (屏幕分辨率 1920x1152)                            │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ 纹理配置:                                                   │  │
│  │ • AlbedoTexture  (GL_RGBA32F) - 漫反射颜色                 │  │
│  │ • NormalTexture   (GL_RGBA32F) - 视图空间法线              │  │
│  │ • PositionTexture (GL_RGBA32F) - 视图空间位置              │  │
│  │ • DepthTexture    (GL_DEPTH_COMPONENT32F) - 深度值         │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  Shader: HalfCornellBox_VS.glsl + HalfCornellBox_FS.glsl        │
│  │                                                               │
│  │  VS: 模型空间 → 世界空间 → 视图空间 → 裁剪空间               │
│  │      输出: v2f_TexCoords, v2f_Normal, v2f_FragPosInViewSpace │
│  │                                                               │
│  │  FS: 采样漫反射纹理 → 输出到 MRT                             │
│  │      layout(location=0) Albedo_                               │
│  │      layout(location=1) Normal_                               │
│  │      layout(location=2) Position_                             │
│  └───────────────────────────────────────────────────────────────┘
└──────────────────────────────────────────────────────────────────┘
```

#### 2.2.2 RSM 缓冲生成阶段 (RSMBufferPass)

```
┌──────────────────────────────────────────────────────────────────┐
│                    RSM 缓冲生成流程                               │
├──────────────────────────────────────────────────────────────────┤
│  输入: 场景几何、光源配置                                        │
│  输出: 3 张纹理 (RSM 分辨率 256x256)                             │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ 光源配置:                                                   │  │
│  │ • 位置: (-0.15, -1.13, -0.58)                              │  │
│  │ • 方向: normalize(-1.0, -0.7071, 0)                        │  │
│  │ • 投影: Ortho(-2, 2, -2, 2, 0.1, 10)                       │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ 纹理配置:                                                   │  │
│  │ • RSMFluxTexture     (GL_RGBA32F) - VPL 光通量             │  │
│  │ • RSMNormalTexture   (GL_RGBA32F) - VPL 法线               │  │
│  │ • RSMPositionTexture (GL_RGBA32F) - VPL 位置               │  │
│  │                                                             │  │
│  │ 边界处理: GL_CLAMP_TO_BORDER, BorderColor=(0,0,0,0)        │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  Shader: RSMBuffer_VS.glsl + RSMBuffer_FS.glsl                  │
│  │                                                               │
│  │  VS: 从光源视角渲染                                          │
│  │      gl_Position = LightVPMatrix * WorldPos                 │
│  │      注意: 仍存储相机空间的位置和法线！                       │
│  │                                                               │
│  │  FS: 计算并输出 VPL 信息                                     │
│  │      Flux = LightColor * DiffuseColor                       │
│  │      Normal = 视图空间法线                                   │
│  │      Position = 视图空间位置                                 │
│  └───────────────────────────────────────────────────────────────┘
└──────────────────────────────────────────────────────────────────┘
```

#### 2.2.3 着色计算阶段 (ShadingWithRSMPass)

```
┌──────────────────────────────────────────────────────────────────┐
│                    Compute Shader 着色流程                        │
├──────────────────────────────────────────────────────────────────┤
│  输入: G-Buffer 纹理、RSM 纹理、VPL 采样数据                      │
│  输出: ShadingTexture (最终着色结果)                              │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ VPL 采样初始化 (InitSampleCoordsAndWeights4VPLs):          │  │
│  │                                                             │  │
│  │  for i in 0..32:                                            │  │
│  │    xi1 = random(0,1)                                        │  │
│  │    xi2 = random(0,1)                                        │  │
│  │    sample[i] = (xi1*sin(2π*xi2), xi1*cos(2π*xi2), xi1², 0) │  │
│  │                                                             │  │
│  │  存储到 Uniform Buffer (binding=1)                          │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ 计算流程 (每个像素一个线程):                                 │  │
│  │                                                             │  │
│  │  1. 读取 G-Buffer 数据                                      │  │
│  │     FragViewNormal = texelFetch(NormalTexture)              │  │
│  │     FragAlbedo = texelFetch(AlbedoTexture)                  │  │
│  │     FragViewPos = texelFetch(PositionTexture)               │  │
│  │                                                             │  │
│  │  2. 计算像素在光源空间的位置                                 │  │
│  │     FragPosInLightSpace = LightVP * Inverse(View) * FragPos │  │
│  │     FragNDCPos4Light = (FragPosInLightSpace.xy + 1) / 2     │  │
│  │                                                             │  │
│  │  3. 直接光照计算                                            │  │
│  │     ┌───────────────────────────────────────────────────┐   │  │
│  │     │ if (超出光源视野范围)                              │   │  │
│  │     │   DirectIllumination = 0.1 * FragAlbedo (环境光)  │   │  │
│  │     │ else                                               │   │  │
│  │     │   DirectIllumination = Albedo * max(dot(-L, N), 0.1)│  │  │
│  │     └───────────────────────────────────────────────────┘   │  │
│  │                                                             │  │
│  │  4. 间接光照计算 (累加 32 个 VPL 贡献)                       │  │
│  │     for i in 0..VPLNum:                                     │  │
│  │       samplePos = FragNDC + MaxRadius * sample[i].xy * TexelSize │
│  │       VPLFlux = texture(RSMFlux, samplePos)                 │  │
│  │       VPLNormal = texture(RSMNormal, samplePos)             │  │
│  │       VPLPos = texture(RSMPosition, samplePos)              │  │
│  │                                                             │  │
│  │       VPL2Frag = normalize(FragPos - VPLPos)                │  │
│  │       irradiance += Flux * max(dot(VPLNormal, VPL2Frag), 0) │  │
│  │                         * max(dot(FragNormal, -VPL2Frag), 0)│  │
│  │                         * weight                             │  │
│  │                                                             │  │
│  │     IndirectIllumination = irradiance * FragAlbedo / VPLNum │  │
│  │                                                             │  │
│  │  5. 合成输出                                                │  │
│  │     Result = DirectIllumination + IndirectIllumination      │  │
│  │     imageStore(OutputImage, FragPos, Result)                │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

#### 2.2.4 屏幕输出阶段 (ScreenQuadPass)

```
┌──────────────────────────────────────────────────────────────────┐
│                    屏幕输出流程                                   │
├──────────────────────────────────────────────────────────────────┤
│  Shader: ScreenQuad_VS.glsl + ScreenQuad_FS.glsl                │
│                                                                  │
│  VS: 顶点位置直接输出 (-1 到 1 的全屏四边形)                      │
│                                                                  │
│  FS: 采样 ShadingTexture → Gamma 校正                            │
│      Color = pow(texture(Texture, uv), 1/2.2)                   │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

---

## 三、核心实体分析

### 3.1 类结构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           类继承关系                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────┐         ┌─────────────────┐                        │
│  │   IGameObject   │         │   IRenderPass   │                        │
│  │    (抽象基类)    │         │    (抽象基类)    │                        │
│  ├─────────────────┤         ├─────────────────┤                        │
│  │ - m_name        │         │ - m_passName    │                        │
│  │ - m_model       │         │ - m_executionOrder│                       │
│  │ - m_modelMatrix │         │ - m_shader      │                        │
│  │ - m_position    │         │ - m_type        │                        │
│  │ - m_rotation    │         ├─────────────────┤                        │
│  │ - m_scale       │         │ + InitV() = 0   │                        │
│  ├─────────────────┤         │ + UpdateV() = 0 │                        │
│  │ + InitV() = 0   │         │ + operator<()   │                        │
│  │ + UpdateV() = 0 │         └────────┬────────┘                        │
│  └────────┬────────┘                  │                                 │
│           │                           │                                 │
│           │ 继承                       │ 继承                            │
│           ▼                           ▼                                 │
│  ┌─────────────────┐    ┌─────────────────────────────────────────────┐│
│  │ HalfCornellBox  │    │              具体渲染通道                    ││
│  ├─────────────────┤    │ ┌─────────────────┐ ┌─────────────────┐     ││
│  │ + InitV()       │    │ │HalfCornellBox   │ │  RSMBufferPass  │     ││
│  │   加载模型      │    │ │  GBufferPass    │ │  执行顺序: 2    │     ││
│  │ + UpdateV()     │    │ │  执行顺序: 1    │ │  输出: RSM 纹理 │     ││
│  │   空实现        │    │ │  输出: G-Buffer │ └─────────────────┘     ││
│  └─────────────────┘    │ └─────────────────┘                         ││
│                         │ ┌─────────────────┐ ┌─────────────────┐     ││
│                         │ │ShadingWithRSM   │ │ ScreenQuadPass  │     ││
│                         │ │     Pass        │ │  执行顺序: 4    │     ││
│                         │ │  执行顺序: 3    │ │  输出: 屏幕     │     ││
│                         │ │  类型: COMPUTE  │ └─────────────────┘     ││
│                         │ │  输出: 着色结果 │                         ││
│                         │ └─────────────────┘                         ││
│                         └─────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.2 核心实体职责

| 实体 | 职责 | 关键成员 |
|------|------|----------|
| `HalfCornellBox` | 场景对象，封装渲染模型 | `m_model` (Sponza 模型) |
| `HalfCornellBoxGBufferPass` | 生成 G-Buffer | `m_FBO`, G-Buffer 纹理 |
| `RSMBufferPass` | 从光源视角生成 RSM | `m_FBO`, `m_resolutionRSM`, `m_directionalLightDirection` |
| `ShadingWithRSMPass` | 计算直接+间接光照 | `m_sampleCoordsAndWeights4VPLs`, `m_cntVPL`, `m_maxSampleRadius` |
| `ScreenQuadPass` | 最终屏幕输出 | - |
| `ResourceManager` | 全局资源管理 | `m_sharedDataMap`, `m_renderPasses`, `m_gameObjects` |

### 3.3 实体依赖关系

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         数据流转与依赖关系                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                      ResourceManager                             │   │
│  │  ┌──────────────────────────────────────────────────────────┐   │   │
│  │  │                    SharedDataMap                          │   │   │
│  │  ├──────────────────────────────────────────────────────────┤   │   │
│  │  │ "AlbedoTexture"    → HalfCornellBoxGBufferPass 生成      │   │   │
│  │  │ "NormalTexture"    → HalfCornellBoxGBufferPass 生成      │   │   │
│  │  │ "PositionTexture"  → HalfCornellBoxGBufferPass 生成      │   │   │
│  │  │ "DepthTexture"     → HalfCornellBoxGBufferPass 生成      │   │   │
│  │  │ "RSMFluxTexture"   → RSMBufferPass 生成                  │   │   │
│  │  │ "RSMNormalTexture" → RSMBufferPass 生成                  │   │   │
│  │  │ "RSMPositionTexture"→ RSMBufferPass 生成                 │   │   │
│  │  │ "LightVPMatrix"    → RSMBufferPass 生成                  │   │   │
│  │  │ "RSMResolution"    → RSMBufferPass 生成                  │   │   │
│  │  │ "LightDir"         → RSMBufferPass 生成                  │   │   │
│  │  │ "ShadingTexture"   → ShadingWithRSMPass 生成             │   │   │
│  │  └──────────────────────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  依赖关系:                                                              │
│                                                                         │
│  HalfCornellBoxGBufferPass ──┐                                         │
│        │                     │                                         │
│        │ 生成 G-Buffer       │                                         │
│        ▼                     ▼                                         │
│  ResourceManager.SharedDataMap ──────────────────────┐                 │
│        │                                             │                 │
│        │ 读取 G-Buffer                               │ 读取 RSM       │
│        ▼                                             ▼                 │
│  ShadingWithRSMPass ◄─────────────────────── RSMBufferPass             │
│        │                                             │                 │
│        │ 生成 ShadingTexture                         │ 生成 RSM       │
│        ▼                                             ▼                 │
│  ResourceManager.SharedDataMap ◄─────────────────────────              │
│        │                                                               │
│        │ 读取 ShadingTexture                                           │
│        ▼                                                               │
│  ScreenQuadPass ──► 屏幕                                               │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 四、关键逻辑点剖析

### 4.1 执行顺序控制机制

```cpp
// main.cpp
RegisterRenderPass(std::make_shared<HalfCornellBoxGBufferPass>(..., 1));
RegisterRenderPass(std::make_shared<RSMBufferPass>(..., 2));
RegisterRenderPass(std::make_shared<ShadingWithRSMPass>(..., 3));
RegisterRenderPass(std::make_shared<ScreenQuadPass>(..., 4));
```

**判断条件**: `executionOrder` 数值越小越先执行

**关键点**: 顺序必须正确，否则会出现纹理未初始化错误

### 4.2 坐标空间转换链

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         坐标空间转换                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  G-Buffer 存储: 视图空间 (Camera View Space)                            │
│                                                                         │
│  RSM 存储: 视图空间 (Camera View Space)                                 │
│  注意: RSM 虽然从光源视角渲染，但存储的位置和法线仍在相机视图空间!        │
│                                                                         │
│  ShadingWithRSM_CS.glsl 中的转换:                                       │
│                                                                         │
│  FragViewPos (相机视图空间)                                             │
│       │                                                                 │
│       │ LightVPMatrix * Inverse(ViewMatrix)                            │
│       ▼                                                                 │
│  FragPosInLightSpace (光源裁剪空间)                                     │
│       │                                                                 │
│       │ 透视除法 + NDC 变换                                             │
│       ▼                                                                 │
│  FragNDCPos4Light (光源 NDC, 范围 [0,1])                                │
│       │                                                                 │
│       │ 用于采样 RSM 纹理                                               │
│       ▼                                                                 │
│  VPL 数据                                                                │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.3 VPL 采样策略

```cpp
// ShadingWithRSMPass.cpp:64-72
void ShadingWithRSMPass::InitSampleCoordsAndWeights4VPLs()
{
    for (int i = 0; i < m_cntVPL; ++i)
    {
        float xi1 = u(e);  // [0, 1]
        float xi2 = u(e);  // [0, 1]
        // 圆盘采样 + 距离衰减权重
        m_sampleCoordsAndWeights4VPLs.emplace_back(
            xi1 * sin(2 * π * xi2),  // x: 极坐标转换
            xi1 * cos(2 * π * xi2),  // y: 极坐标转换
            xi1 * xi1,               // z: 权重 (距离平方衰减)
            0
        );
    }
}
```

**采样特点**:
- 使用圆盘采样模式
- 权重与距离平方成正比，模拟真实的间接光照衰减
- 采样位置是相对于像素在光源空间中的投影位置

### 4.4 直接光照边界处理

```glsl
// ShadingWithRSM_CS.glsl:55-58
if (FragPosInLightSpace.z < 0.0f || 
    FragPosInLightSpace.x > 1.0f || FragPosInLightSpace.y > 1.0f || 
    FragPosInLightSpace.x < 0.0f || FragPosInLightSpace.y < 0.0f)
    DirectIllumination = vec3(0.1) * FragAlbedo;  // 仅环境光
else
    DirectIllumination = FragAlbedo * max(dot(-u_LightDirInViewSpace, FragViewNormal), 0.1);
```

**分支条件**:
- 判断像素是否在光源视野范围内
- 超出范围时仅使用环境光(0.1 倍 Albedo)
- 在范围内时计算标准 Lambert 漫反射

### 4.5 参数传递路径

| 参数 | 产生位置 | 消费位置 | 传递方式 |
|------|----------|----------|----------|
| Albedo/Normal/Position Texture | GBufferPass | ShadingPass | ResourceManager |
| RSM Flux/Normal/Position Texture | RSMBufferPass | ShadingPass | ResourceManager |
| LightVPMatrix | RSMBufferPass | ShadingPass | ResourceManager |
| LightDir | RSMBufferPass | ShadingPass | ResourceManager |
| VPL Samples | ShadingPass.Init | ShadingPass.Update | Uniform Buffer |
| ShadingTexture | ShadingPass | ScreenQuadPass | ResourceManager |

---

## 五、逻辑闭环验证

### 5.1 功能完整性检查

| 功能需求 | 实现状态 | 验证点 |
|----------|----------|--------|
| 场景加载 | ✅ 完整 | `HalfCornellBox::InitV()` 加载 Sponza 模型 |
| G-Buffer 生成 | ✅ 完整 | 4 张纹理正确配置并渲染 |
| RSM 生成 | ✅ 完整 | 3 张纹理从光源视角正确生成 |
| 直接光照 | ✅ 完整 | 方向光 Lambert 漫反射 |
| 间接光照 | ✅ 完整 | 32 个 VPL 累加贡献 |
| 屏幕输出 | ✅ 完整 | Gamma 校正后输出 |
| 资源共享 | ✅ 完整 | 通过 ResourceManager 传递所有数据 |

### 5.2 边界条件分析

#### 5.2.1 已处理的边界条件

| 边界条件 | 处理方式 | 代码位置 |
|----------|----------|----------|
| 像素超出光源视野 | 使用环境光代替 | `ShadingWithRSM_CS.glsl:55` |
| RSM 纹理边界采样 | `GL_CLAMP_TO_BORDER` + 黑色边界 | `RSMBufferPass.cpp:28-30` |
| 负法线点积 | `max(dot, 0)` 确保非负 | `ShadingWithRSM_CS.glsl:37` |
| 零除风险 | 使用 `u_VPLNum` 作为除数，固定为 32 | `ShadingWithRSM_CS.glsl:72` |

#### 5.2.2 潜在问题与建议

| 问题 | 风险等级 | 建议修复 |
|------|----------|----------|
| VPL 数量硬编码 | ⚠️ 中 | 在 Compute Shader 中 `#define VPL_NUM 32`，与 C++ 端 `m_cntVPL` 同步，若不一致会导致 `if(u_VPLNum != VPL_NUM) return;` 提前退出 |
| 模型路径硬编码 | ⚠️ 低 | `"../Model/sponza/sponza.obj"` 应通过配置文件指定 |
| 无错误处理 | ⚠️ 中 | `dynamic_pointer_cast` 失败时无检查，纹理/FBO 生成无错误验证 |
| 固定光源参数 | ⚠️ 低 | 光源位置、方向硬编码，无法动态调整 |
| Gamma 校正不完整 | ⚠️ 中 | G-Buffer 中读取颜色时未进行 Linear 空间转换（`RSMBuffer_FS.glsl:17` 注释掉了 `pow(TexelColor, 2.2)`） |

### 5.3 逻辑断点检查

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         逻辑断点检查清单                                 │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ✅ 初始化顺序: GameObject → RenderPass (按 executionOrder)            │
│  ✅ 纹理生成: 在使用前完成创建和注册                                     │
│  ✅ FBO 绑定: 每个 Pass 在 UpdateV 开头正确绑定                         │
│  ✅ 视口设置: RSMBufferPass 正确设置/恢复视口                           │
│  ✅ Compute Shader 同步: glMemoryBarrier 确保写入完成                   │
│  ✅ Uniform Buffer 绑定: binding=1 正确配置                             │
│                                                                         │
│  ⚠️ 潜在断点:                                                           │
│  • ResourceManager::GetSharedDataByName 使用 boost::any_cast           │
│    若类型不匹配会抛出异常，需确保类型一致                                 │
│  • Shader 编译错误无处理，可能运行时崩溃                                  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.4 性能考量

| 参数 | 当前值 | 影响 |
|------|--------|------|
| RSM 分辨率 | 256×256 | 较低，影响间接光照质量，可提升至 512 或 1024 |
| VPL 采样数 | 32 | 较低，可能导致间接光照噪声，可增加至 64-128 |
| 最大采样半径 | 25 | 控制间接光照范围 |
| 窗口分辨率 | 1920×1152 | Compute Shader 工作组数量随之变化 |

---

## 六、总结

本项目完整实现了一个基于 RSM 的全局光照渲染器，采用延迟渲染架构，通过以下核心流程实现间接光照：

1. **G-Buffer 生成**: 从相机视角存储场景几何信息
2. **RSM 生成**: 从光源视角生成虚拟点光源数据
3. **Compute Shader 着色**: 结合直接光照和 VPL 采样的间接光照
4. **屏幕输出**: Gamma 校正后显示最终结果

代码结构清晰，通过 `IRenderPass` 抽象实现可扩展的渲染管线，通过 `ResourceManager` 实现数据共享。主要改进方向包括增加错误处理、支持动态光源配置、提升 RSM 分辨率和 VPL 采样数以提高视觉质量。
