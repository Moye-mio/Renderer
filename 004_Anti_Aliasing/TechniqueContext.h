#pragma once
// ============================================================================
// 004_Anti_Aliasing - TechniqueContext
//
// 对比实验的 CPU 侧共享状态（不持 GPU 资源）。
// None / MSAA / FXAA / TAA；SMAA 后续按枚举接入。
// ============================================================================
#include <cstdint>

enum class AATechnique
{
    None = 0,
    MSAA = 1,
    FXAA = 2,
    SMAA = 3,
    TAA  = 4,
};

struct TechniqueContext
{
    AATechnique mode = AATechnique::None;

    // MSAA 采样数（2 / 4 / 8）；实际创建时会钳到设备 maxColorSampleCount。
    uint32_t msaaSamples = 4;

    // FXAA（Lottes 3.11 Quality 默认值）：子像素混合、相对/暗区对比度阈值。
    float fxaaSubpix = 0.75f;
    float fxaaEdgeThreshold = 0.166f;
    float fxaaEdgeThresholdMin = 0.0833f;

    // TAA：当前帧混合权重（其余来自 history）；1=只用当前，偏小更稳也更糊。
    float taaFeedback = 0.18f;
    // 0 = 不 clamp，1 = YCoCg 邻域 AABB，2 = RGB variance clip。
    int taaClampMode = 1;
    // Halton 子像素偏移倍率；0 等于关掉 jitter（只剩时域混合）。
    float taaJitterScale = 0.5f;
    // 切到 TAA / 手动重置时由 overlay 置位，Pass 消费后清掉。
    bool taaResetHistory = false;

    // 世界空间主光方向（指向光源），与 Scene_FS 的 Lambert 对齐。
    float lightYawDeg   = 35.0f;
    float lightPitchDeg = 45.0f;
    float ambient       = 0.12f;
};
