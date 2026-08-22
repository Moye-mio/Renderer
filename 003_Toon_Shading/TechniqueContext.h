#pragma once
// ============================================================================
// 003_Toon_Shading - TechniqueContext
//
// 对比实验的 CPU 侧共享状态（不持 GPU 资源）。
// ============================================================================

enum class ToonTechnique
{
    DiffuseOnly = 0,
    CelRamp     = 1,
};

struct TechniqueContext
{
    ToonTechnique mode = ToonTechnique::CelRamp;

    // 世界空间主光方向（指向光源）。
    float lightYawDeg   = 8.0f;
    float lightPitchDeg = 22.0f;
    float ambient       = 0.08f;

    // 半 Lambert 重映射：dark → grey → bright。须保持 dark < grey < bright。
    // 交界收窄，侧面光下才能看出色阶（正面光会整身落在亮部）。
    float brightFac = 0.52f;
    float greyFac   = 0.47f;
    float darkFac   = 0.12f;
    bool  nightRamp = false;
};
