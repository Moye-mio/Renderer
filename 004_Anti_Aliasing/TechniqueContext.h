#pragma once
// ============================================================================
// 004_Anti_Aliasing - TechniqueContext
//
// 对比实验的 CPU 侧共享状态（不持 GPU 资源）。
// technique0 = None（无 AA 基线），technique1 = MSAA。
// FXAA / SMAA / TAA 后续按枚举接入。
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

    // 世界空间主光方向（指向光源），与 Scene_FS 的 Lambert 对齐。
    float lightYawDeg   = 35.0f;
    float lightPitchDeg = 45.0f;
    float ambient       = 0.12f;
};
