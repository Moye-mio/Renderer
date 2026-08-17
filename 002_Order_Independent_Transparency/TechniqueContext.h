#pragma once
// ============================================================================
// 002_Order_Independent_Transparency - TechniqueContext
//
// 对比实验的 CPU 侧共享状态（不持 GPU 资源）：
//   mode   —— 当前透明度算法。Baseline 是朴素 Alpha（不是 OIT）；
//             WeightedBlended 走 Accum / Revealage / Blend。
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

struct TechniqueContext
{
    OITTechnique mode = OITTechnique::Baseline;

    float dragonOpacity = 0.40f;

    // 文档默认：针对 0–20 视空间深度。当前相机 far=20，盒子尺度匹配。
    float weighted1    = 2.0f;
    float weighted2    = 20.0f;
    float weighted1Exp = 1.5f;
    float weighted2Exp = 3.0f;
};
