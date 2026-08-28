#pragma once
// ============================================================================
// 003_Toon_Shading - TechniqueContext
//
// 对比实验的 CPU 侧共享状态（不持 GPU 资源）。
// ============================================================================
#include "Basic/TitusMath.h"

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

    // 背面外扩描边（正面剔除 + 沿平滑法线挤出，屏幕空间等宽）。
    bool  enableOutline = true;
    float outlinePixels = 2.5f;
    // 外扩壳与正面在轮廓处近似共面，深度比较落在临界值会出麻点。硬件 depth
    // bias 在 RasterizerState 里没有，只能在 shader 里手动偏移。单位是米，在
    // 视空间沿相机方向挪——NDC 偏移随距离剧烈变化，调不出通用值。
    float outlineZBias  = 0.0f;

    // 线宽随视距收细：scale = pow(clamp(refDist / viewZ, 0, 1), falloffPower)。
    // 近于 refDist 时 clamp 到 1，保证靠近相机不会变粗（屏幕空间等宽）。
    // 低于 1 像素线会碎成点串，minPixels 兜住这个下限。
    float outlineMinPixels    = 0.8f;
    float outlineMaxPixels    = 6.0f;
    float outlineRefDistance  = 3.4f;
    float outlineFalloffPower = 0.5f;

    // 远处把描边色往 fadeColor 拉，避免小人被粗黑线糊成一团。
    // 不用 alpha 混合：外扩壳在同一像素上会有多层背面重叠，混合会叠出深色斑块。
    float outlineFadeStart    = 8.0f;
    float outlineFadeEnd      = 25.0f;
    float outlineFadeStrength = 0.85f;
    TitusMath::Vec3 outlineFadeColor{0.30f, 0.30f, 0.34f};

    // 按 NilouMaterials::Part 索引：Body / Dress / Hair / Face。
    // 顶点里只烘 partIndex，颜色与线宽倍率放这里，可以实时调。
    TitusMath::Vec3 outlinePartColor[4] = {
        {0.06f, 0.04f, 0.07f}, // Body
        {0.06f, 0.04f, 0.07f}, // Dress
        {0.06f, 0.04f, 0.07f}, // Hair
        {0.06f, 0.04f, 0.07f}, // Face
    };
    // 脸偏细是卡渲通例：整脸等宽会把五官压得很脏。倍率为 0 即该部件不描边。
    float outlinePartWidth[4] = {1.0f, 1.0f, 1.0f, 0.6f};
};
