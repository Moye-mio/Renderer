#pragma once
// ============================================================================
// 000_Deferred_Shading - DeferredLightingPass
//
// 延迟渲染的光照 Pass：一个全屏三角形 Pass，从共享数据黑板取 G-Buffer 的
// Albedo / Normal(view) / Position(view) 三张纹理，对 N（默认 5）个点光源做
// Blinn-Phong 累加，直接把结果输出到默认 backbuffer。
//
// 光源以世界坐标描述；每帧用主相机 view 矩阵把光源位置变换到视空间后写入
// std140 UBO（因为 G-Buffer 存的是视空间位置/法线）。
// ============================================================================
#include <vector>

#include "RendererInterface/TitusGfxPass.h"

class DeferredLightingPass : public TitusRHI::IRenderPass
{
public:
    // 业务侧配置的点光源（世界空间）
    struct PointLightDesc
    {
        TitusMath::Vec3 worldPos{0.0f};
        float     radius = 10.0f;
        TitusMath::Vec3 color{1.0f};
        float     intensity = 3.0f;
    };

    DeferredLightingPass();
    ~DeferredLightingPass() override = default;

    void SetLights(const std::vector<PointLightDesc>& lights) { m_lights = lights; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

    static constexpr int MAX_LIGHTS = 5;

private:
    // 与 DeferredLighting_FS.glsl 的 std140 u_LightBlock 严格对齐（总计 176B）。
    struct GpuPointLight
    {
        TitusMath::Vec4 positionVSAndRadius{0.0f}; // xyz: 视空间位置, w: 半径
        TitusMath::Vec4 colorAndIntensity{0.0f};   // rgb: 颜色,       w: 强度
    };
    struct LightBlockData
    {
        GpuPointLight lights[MAX_LIGHTS];
        TitusMath::IVec4 count{0}; // x = 有效光源数
    };

    std::vector<PointLightDesc> m_lights;

    TitusRHI::SamplerHandle m_sampler;
    TitusRHI::ShaderHandle m_vs;
    TitusRHI::ShaderHandle m_fs;
    TitusRHI::PipelineHandle m_pipeline;
    TitusRHI::BufferHandle m_lightUbo;
};
