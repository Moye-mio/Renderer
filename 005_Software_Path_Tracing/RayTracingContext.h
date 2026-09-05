#pragma once
// ============================================================================
// 005_Software_Path_Tracing - RayTracingContext
//
// 光追测试台的 CPU 侧共享状态（不持 GPU 资源）：算法模式、采样参数与曝光。
// RayTracePass 每帧读它打成 UBO；overlay 改动这里的任何一项都要重置累积，
// 由 accumDirty 通知 Pass。
// ============================================================================
#include <cstdint>

enum class RTTechnique
{
    Normal           = 0, // 法线可视化：只验证求交与法线朝向，不做光照
    DirectLight      = 1, // 面光源直接光 + Lambert，多采样得到软阴影
    AmbientOcclusion = 2, // 半球余弦采样的射线 AO
    PathTracing      = 3, // 路径追踪：余弦重要性采样 + 可选 NEE + 俄罗斯轮盘
};

// 显示阶段的色调映射方式。
enum class RTToneMap
{
    None     = 0, // 只做曝光 + gamma，便于看原始能量是否溢出
    Reinhard = 1,
    ACES     = 2,
};

struct RayTracingContext
{
    RTTechnique mode = RTTechnique::PathTracing;

    // 每帧发多少条主光线；越大越快收敛，但单帧越卡。
    int samplesPerFrame = 2;

    // 路径追踪的最大弹射次数（1 = 只有直接光）。
    int maxBounces = 8;

    // NEE（Next Event Estimation）：每次弹射额外对面光源采一个点。
    // 关掉就只靠余弦采样"撞"到天花板的灯，噪点会明显大一截，
    // 这个对照是本示例最直观的一组。
    bool enableNee = true;

    // AO 模式的射线长度与每像素采样数。
    float aoRadius  = 1.0f;
    int   aoSamples = 8;

    // DirectLight 模式每像素对面光源的采样数（1 = 硬阴影感的噪点，越大越软）。
    int lightSamples = 4;

    // 显示阶段：曝光与色调映射。改这两项不需要重置累积（累积存的是线性均值）。
    float     exposure = 1.0f;
    RTToneMap toneMap  = RTToneMap::ACES;

    // 相机静止时逐帧累加样本；相机一动或任何采样参数变化就要清空重来。
    // overlay / main 置位，Pass 消费后清掉。
    bool accumDirty = false;

    // 累积上限，到了就停下不再重复计算（0 = 不限制）。
    int maxAccumSamples = 4096;

    // 由 Pass 回填给 overlay 显示：已累积的样本数。
    uint32_t accumulatedSamples = 0;
};
