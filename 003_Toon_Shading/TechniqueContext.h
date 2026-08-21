#pragma once
// ============================================================================
// 003_Toon_Shading - TechniqueContext
//
// 对比实验的 CPU 侧共享状态（不持 GPU 资源）。
// M1 只有 DiffuseOnly；M2 起再加 Ramp / Outline / FaceSDF。
// ============================================================================

enum class ToonTechnique
{
    DiffuseOnly = 0,
};

struct TechniqueContext
{
    ToonTechnique mode = ToonTechnique::DiffuseOnly;

    // 世界空间主光方向（指向光源）。ImGui 可调，M1 已用于漫反射方向光。
    float lightYawDeg   = 35.0f;
    float lightPitchDeg = 50.0f;
    float ambient       = 0.22f;
};
