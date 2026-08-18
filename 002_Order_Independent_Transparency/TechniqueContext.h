#pragma once
// ============================================================================
// 002_Order_Independent_Transparency - TechniqueContext
//
// 对比实验的 CPU 侧共享状态（不持 GPU 资源）：
//   mode   —— 当前透明度算法。Baseline 是朴素 Alpha（不是 OIT）；
//             WeightedBlended 走 Accum / Revealage / Blend。
//   drawOrder —— 半透明龙的提交顺序，两种算法都受它控制。这是本示例的核心
//               对照开关：Baseline 下换顺序画面明显跳变，WBOIT 下纹丝不动。
//   dragonOpacity —— 所有半透明龙共用的不透明度，ImGui 可调。
//                   每只龙的颜色在 Scene::DragonInstance 上。
//   weighted* —— WBOIT 深度权重（McGuire _WEIGHTED0），仅 WeightedBlended 使用。
// ============================================================================

enum class OITTechnique
{
    Baseline = 0,        // 不透明 Cornell + 朴素 SrcAlpha 混合画龙；不是 OIT
    WeightedBlended = 1, // Weighted Blended OIT（Accum / Revealage / Blend）
    // Fourier,
    // Wavelet,
};

// 半透明实例的提交顺序。BackToFront 是 per-object 排序能达到的最优解——在龙
// 互相穿插的构型下它依然修不好咬合处，那正是 OIT 要解决的问题。
enum class DragonDrawOrder
{
    SceneOrder = 0,  // 不排序，按实例数组原序提交
    BackToFront = 1, // 按实例中心视距由远到近
    FrontToBack = 2, // 由近到远，最坏顺序
};

struct TechniqueContext
{
    OITTechnique mode = OITTechnique::Baseline;

    DragonDrawOrder drawOrder = DragonDrawOrder::SceneOrder;

    // 排序误差正比于 a_f * a_b，0.4 时只有 0.16；提到 0.55 让差异更容易看见。
    float dragonOpacity = 0.55f;

    // 文档默认：针对 0–20 视空间深度。当前相机 far=20，盒子尺度匹配。
    float weighted1    = 2.0f;
    float weighted2    = 20.0f;
    float weighted1Exp = 1.5f;
    float weighted2Exp = 3.0f;
};
