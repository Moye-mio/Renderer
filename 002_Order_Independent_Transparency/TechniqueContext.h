#pragma once
// ============================================================================
// 002_Order_Independent_Transparency - TechniqueContext
//
// 对比实验的 CPU 侧共享状态（不持 GPU 资源）：
//   mode   —— 当前透明度算法。V1 只有 Baseline（朴素 Alpha，不是 OIT）。
//   dragonOpacity —— 所有半透明龙共用的不透明度，ImGui 可调。
//                   每只龙的颜色在 Scene::DragonInstance 上。
//
// WBOIT / 傅里叶 OIT / 小波 OIT 后续接到同一套 Scene，只换 Transparent 侧 Pass。
// ============================================================================

enum class OITTechnique
{
    Baseline = 0, // 不透明 Cornell + 朴素 SrcAlpha 混合画龙；不是 OIT
    // WeightedBlended,
    // Fourier,
    // Wavelet,
};

struct TechniqueContext
{
    OITTechnique mode = OITTechnique::Baseline;

    float dragonOpacity = 0.40f;
};
