#pragma once
// ============================================================================
// 000_Forward_Deferred_ForwardPlus - ForwardPlusPass
//
// Clustered Forward：同一 Record 内五阶段
//   1) Depth     —— 几何写 R32F 视空间 Z + D32
//   2) TileDepth —— Compute 把 R32F 归约成每 tile 的 [minDist, maxDist] 视距
//   3) Cull      —— 一 workgroup 一 cluster，用 tile 深度收紧 AABB 后并行剔点光
//   4) Shade     —— 离屏 RT 复用 (1) 的 D32（LessOrEqual + 不写深度），overdraw = 1
//   5) Resolve   —— 离屏颜色 1:1 拷回 backbuffer
// mode != ForwardPlus 时 Record 早退。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class Sponza;
struct TechniqueContext;

class ForwardPlusPass : public TitusRHI::IRenderPass
{
public:
    ForwardPlusPass();
    ~ForwardPlusPass() override = default;

    void SetSponza(Sponza* sponza) { m_sponza = sponza; }
    void SetContext(TechniqueContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    void RecordDepth(TitusRHI::IGDevice& device, TitusRHI::RenderCommandList& cmd);
    void RecordTileDepth(TitusRHI::IGDevice& device, TitusRHI::RenderCommandList& cmd);
    void RecordCull(TitusRHI::IGDevice& device, TitusRHI::RenderCommandList& cmd);
    void RecordShade(TitusRHI::IGDevice& device, TitusRHI::RenderCommandList& cmd);
    void RecordResolve(TitusRHI::IGDevice& device, TitusRHI::RenderCommandList& cmd);

    Sponza* m_sponza = nullptr;
    TechniqueContext* m_ctx = nullptr;

    uint32_t m_width = 1920;
    uint32_t m_height = 1152;
    uint32_t m_tilesX = 1;
    uint32_t m_tilesY = 1;

    // Depth 预通道：R32F 视空间 Z + D32
    TitusRHI::TextureHandle m_depthVSTex;
    TitusRHI::TextureHandle m_depthTex;
    TitusRHI::RenderTargetHandle m_depthRT;
    TitusRHI::ShaderHandle m_depthVS;
    TitusRHI::ShaderHandle m_depthFS;
    TitusRHI::PipelineHandle m_depthPipeline;
    TitusRHI::SamplerHandle m_depthSampler;

    // TileDepth Compute：R32F -> 每 tile [minDist, maxDist]
    TitusRHI::ShaderHandle m_tileDepthCS;
    TitusRHI::PipelineHandle m_tileDepthPipeline;

    // Cull Compute
    TitusRHI::ShaderHandle m_cullCS;
    TitusRHI::PipelineHandle m_cullPipeline;

    // Shade：离屏颜色 + 复用预通道 D32
    TitusRHI::TextureHandle m_shadeColorTex;
    TitusRHI::RenderTargetHandle m_shadeRT;
    TitusRHI::ShaderHandle m_shadeVS;
    TitusRHI::ShaderHandle m_shadeFS;
    TitusRHI::PipelineHandle m_shadePipeline;

    // Resolve：离屏颜色 -> backbuffer
    TitusRHI::ShaderHandle m_resolveVS;
    TitusRHI::ShaderHandle m_resolveFS;
    TitusRHI::PipelineHandle m_resolvePipeline;

    TitusRHI::BufferHandle m_matricesUbo;
    TitusRHI::BufferHandle m_lightUbo;
    TitusRHI::BufferHandle m_cullParamsUbo;
    TitusRHI::BufferHandle m_tileLightSSBO;
    TitusRHI::BufferHandle m_tileDepthSSBO;
};
