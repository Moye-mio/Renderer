#pragma once
// ============================================================================
// 000_Deferred_Shading - TechniqueContext
//
// 对比实验的 CPU 侧共享状态（不持 GPU 资源）：
//   mode        —— ImGui 切换的当前算法
//   shared      —— 各算法必须一致的输入（灯 / BRDF 常数 / LightBlock UBO 布局）
//   deferred    —— 仅 Deferred 认识（G-Buffer debug 视图）
//   forward     —— 仅 Forward 认识（V1 空占位）
//   forwardPlus —— 仅 Forward+ 认识（tile 尺寸 / 每 tile 灯上限 / debug 视图）
// ============================================================================
#include <algorithm>
#include <vector>

#include "TitusMath.h"

enum class ShadingTechnique
{
    Deferred    = 0,
    Forward     = 1,
    ForwardPlus = 2,
};

// 业务侧点光源（世界空间）。各算法都从 SharedShadingParams::lights 读取。
struct PointLightDesc
{
    TitusMath::Vec3 worldPos{0.0f};
    float           radius = 10.0f;
    TitusMath::Vec3 color{1.0f};
    float           intensity = 3.0f;
};

struct SharedShadingParams
{
    static constexpr int MAX_LIGHTS = 1000;

    // 与 DeferredLighting_FS / Forward_FS 的 std140 u_LightBlock 严格对齐。
    // PointLight = 32B；数组 1000 个 = 32000B；其后 ivec4 = 16B；总计 32016B。
    struct GpuPointLight
    {
        TitusMath::Vec4 positionVSAndRadius{0.0f}; // xyz: 视空间位置, w: 半径
        TitusMath::Vec4 colorAndIntensity{0.0f};   // rgb: 颜色,       w: 强度
    };
    struct LightBlockData
    {
        GpuPointLight   lights[MAX_LIGHTS];
        TitusMath::IVec4 count{0}; // x = 有效光源数
    };
    static_assert(sizeof(GpuPointLight) == 32, "GpuPointLight std140 size");
    static_assert(sizeof(LightBlockData) == 32016, "LightBlockData std140 size");

    std::vector<PointLightDesc> lights;

    // 与各算法 fragment shader 硬编码值对齐；V1 不进 UBO，改常数时各 shader 一起改。
    float ambient   = 0.08f;
    float shininess = 32.0f;

    void FillLightBlock(LightBlockData& out, const TitusMath::Mat4& view) const
    {
        out = LightBlockData{};
        const int n = static_cast<int>(std::min(lights.size(), static_cast<size_t>(MAX_LIGHTS)));
        for (int i = 0; i < n; ++i)
        {
            const TitusMath::Vec4 posVS = view * TitusMath::Vec4(lights[i].worldPos, 1.0f);
            out.lights[i].positionVSAndRadius = TitusMath::Vec4(TitusMath::Vec3(posVS), lights[i].radius);
            out.lights[i].colorAndIntensity   = TitusMath::Vec4(lights[i].color, lights[i].intensity);
        }
        out.count = TitusMath::IVec4(n, 0, 0, 0);
    }
};

struct DeferredParams
{
    enum class DebugView
    {
        Final    = 0,
        Albedo   = 1,
        Normal   = 2,
        Position = 3,
    };
    DebugView debugView = DebugView::Final;
};

// V1 无 Forward 私有旋钮；占位以免后续加字段时改 Context 形状。
struct ForwardParams
{
};

// Forward+：16×16 tile + Compute 视锥/深度范围剔灯。
// TILE_SIZE / MAX_LIGHTS_PER_TILE / TILE_STRIDE 必须与
// ForwardPlusCull_CS.glsl / ForwardPlus_FS.glsl 的 #define 对齐。
struct ForwardPlusParams
{
    static constexpr int TILE_SIZE = 16;
    static constexpr int MAX_LIGHTS_PER_TILE = 256;
    static constexpr int TILE_STRIDE = 1 + MAX_LIGHTS_PER_TILE; // count + indices

    enum class DebugView
    {
        Final       = 0,
        TileHeatmap = 1,
    };
    DebugView debugView = DebugView::Final;
};

struct TechniqueContext
{
    ShadingTechnique    mode = ShadingTechnique::Deferred;
    SharedShadingParams shared;
    DeferredParams      deferred;
    ForwardParams       forward;
    ForwardPlusParams   forwardPlus;
};
