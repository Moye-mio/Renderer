#pragma once
// ============================================================================
// 006_Dynamic_Diffuse_GI - DDGIPass
// Lighting：Compute rayQuery 更新 probe（Trace → Blend → Border）
// OpaqueShading：延迟合成直接光 + DDGI，可选画出 probe 小球。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"
#include "AssetLoader/AssetTypes.h"
#include "SceneAccel.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

struct DDGIContext;

class DDGIPass : public TitusRHI::IRenderPass
{
public:
    DDGIPass();
    ~DDGIPass() override = default;

    void SetContext(DDGIContext* ctx) { m_ctx = ctx; }
    void SetModel(const TitusAsset::ModelAssetData* model) { m_model = model; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

    uint32_t GetProbeCount() const { return m_probeCount; }
    int GetGridX() const { return m_gridX; }
    int GetGridY() const { return m_gridY; }
    int GetGridZ() const { return m_gridZ; }

private:
    bool CreateComputePipelines(TitusRHI::IGDevice& device, const std::string& shaderDir);
    bool CreateShadePipeline(TitusRHI::IGDevice& device, const std::string& shaderDir);
    bool CreateProbePipeline(TitusRHI::IGDevice& device, const std::string& shaderDir);
    void UpdateVolumeUBO(TitusRHI::IGDevice& device);
    void RecordProbeUpdate(TitusRHI::RenderCommandList& cmd);
    void RecordShadowMask(TitusRHI::RenderCommandList& cmd);
    void RecordShading(TitusRHI::RenderCommandList& cmd);

    // UBO 走 per-frame ring：UpdateBuffer 是直接写常驻映射指针，单份 UBO 会被
    // CPU 在上一帧还在 GPU 上执行时改掉。对 DDGI 尤其致命——Trace 与 Blend 各自
    // 用 UBO 里的旋转矩阵重算同一套射线方向，撕裂会让两趟的方向不一致。
    // 槽位按 GetCurrentFrameIndex() 惰性扩容，不必知道 framesInFlight。
    TitusRHI::BufferHandle AcquireFrameBuffer(TitusRHI::IGDevice& device,
                                              std::vector<TitusRHI::BufferHandle>& ring,
                                              uint32_t frameIndex,
                                              size_t bytes,
                                              const char* debugName);

    DDGIContext* m_ctx = nullptr;
    const TitusAsset::ModelAssetData* m_model = nullptr;
    SceneAccel m_accel;

    static constexpr int kOctRes = 8;
    static constexpr int kRaysPerProbe = 64;
    int m_gridX = 8;
    int m_gridY = 6;
    int m_gridZ = 12;
    uint32_t m_probeCount = 0;
    uint32_t m_probeTexelSize = 10;
    uint32_t m_atlasW = 1;
    uint32_t m_atlasH = 1;
    TitusMath::Vec3 m_probeOrigin{0.0f};
    TitusMath::Vec3 m_probeSpacing{1.0f};

    uint32_t m_width = 1920;
    uint32_t m_height = 1080;
    uint32_t m_frameIndex = 0;
    int m_writeIndex = 0;

    std::vector<TitusRHI::BufferHandle> m_volumeUbos;
    std::vector<TitusRHI::BufferHandle> m_matricesUbos;
    TitusRHI::BufferHandle m_volumeUbo;    // 本帧槽位，Record 开头选定
    TitusRHI::BufferHandle m_matricesUbo;  // 同上
    TitusRHI::BufferHandle m_rayHits;

    TitusRHI::TextureHandle m_irradiance[2];
    TitusRHI::TextureHandle m_distance[2];
    TitusRHI::TextureHandle m_shadowMask;
    TitusRHI::SamplerHandle m_linearSampler;
    TitusRHI::SamplerHandle m_pointSampler;

    // 本帧的射线集旋转矩阵（三列），Trace 与 Blend 共用。
    float m_rayRotation[3][3]{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    std::mt19937 m_rng{0x5eed1234u};

    TitusRHI::ShaderHandle m_traceCS;
    TitusRHI::ShaderHandle m_blendCS;
    TitusRHI::ShaderHandle m_borderCS;
    TitusRHI::ShaderHandle m_shadowCS;
    TitusRHI::PipelineHandle m_tracePipeline;
    TitusRHI::PipelineHandle m_blendPipeline;
    TitusRHI::PipelineHandle m_borderPipeline;
    TitusRHI::PipelineHandle m_shadowPipeline;

    TitusRHI::ShaderHandle m_blitVS;
    TitusRHI::ShaderHandle m_shadeFS;
    TitusRHI::PipelineHandle m_shadePipeline;
    TitusRHI::ShaderHandle m_probeVS;
    TitusRHI::ShaderHandle m_probeFS;
    TitusRHI::PipelineHandle m_probePipeline;

    bool m_rtReady = false;
};
