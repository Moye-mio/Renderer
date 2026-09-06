#pragma once
// ============================================================================
// 006_Dynamic_Diffuse_GI - DDGIContext
// 运行时参数：观察模式、太阳光、probe 时域混合。网格尺寸在 Init 时按
// Sponza AABB 算死，不在 overlay 里改（改了要重建 atlas / AS 无关资源）。
// ============================================================================
#include "TitusMath.h"

enum class DDGIViewMode : int
{
    Combined = 0,
    DirectOnly,
    GIOnly,
    Albedo,
    Normal,
};

struct DDGIContext
{
    DDGIViewMode viewMode = DDGIViewMode::Combined;
    bool showProbes = false;
    bool resetAccumulation = true;

    float giIntensity = 1.35f;
    float hysteresis = 0.97f;
    float maxRayDistance = 18.0f;
    float normalBias = 0.25f;
    float probeVisualScale = 0.06f;
    // 命中点回采上一帧 probe 场的系数。反馈增益是 albedo * bounceScale，
    // 必须 < 1 否则时域迭代会自激发散，这里留出余量。
    float bounceScale = 0.85f;

    // 太阳的照射方向（从光源指向场景，所以默认朝下）。shader 里的 u_LightDir
    // 约定是「指向光源」，取反在 DDGIPass::UpdateVolumeUBO 里做。
    TitusMath::Vec3 lightDir{0.32f, -1.0f, 0.18f};
    TitusMath::Vec3 lightColor{1.0f, 0.96f, 0.88f};
    float lightIntensity = 3.2f;
    TitusMath::Vec3 skyColor{0.42f, 0.55f, 0.78f};

    bool rayTracingReady = false;
};
