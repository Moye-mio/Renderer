#pragma once
// ============================================================================
// 005_Software_Path_Tracing - RayTracePass
//
// 两趟全屏三角形：
//   1) Trace   —— 解析求交 + 按模式着色，结果混进累积缓冲（RGBA32F 双缓冲，
//                 读一张写一张），存的是线性均值。
//   2) Display —— 累积均值做曝光 + 色调映射 + gamma，写回 backbuffer。
//
// 相机一动、采样参数一改、窗口一缩放，就把样本数清零重新累积。
// ============================================================================
#include "RendererInterface/TitusGfxPass.h"

class CornellBoxScene;
struct RayTracingContext;

class RayTracePass : public TitusRHI::IRenderPass
{
public:
    RayTracePass();
    ~RayTracePass() override = default;

    void SetScene(CornellBoxScene* scene) { m_scene = scene; }
    void SetContext(RayTracingContext* ctx) { m_ctx = ctx; }

    void Init(TitusRHI::IGDevice& device) override;
    void Destroy(TitusRHI::IGDevice& device) override;
    void Record(TitusRHI::IGDevice& device,
                TitusRHI::RenderCommandList& cmd,
                uint32_t frameIndex,
                uint32_t imageIndex) override;

private:
    void EnsureTargets(TitusRHI::IGDevice& device, uint32_t width, uint32_t height);
    void DestroyTargets(TitusRHI::IGDevice& device);

    // 相机或采样参数变了就返回 true —— 此时旧样本已经不再描述同一幅图。
    bool ShouldResetAccumulation(const TitusMath::Mat4& view,
                                 const TitusMath::Mat4& proj) const;

    CornellBoxScene*   m_scene = nullptr;
    RayTracingContext* m_ctx = nullptr;

    uint32_t m_width = 0;
    uint32_t m_height = 0;

    TitusRHI::ShaderHandle   m_blitVS;
    TitusRHI::ShaderHandle   m_traceFS;
    TitusRHI::PipelineHandle m_tracePipeline;

    TitusRHI::ShaderHandle   m_displayFS;
    TitusRHI::PipelineHandle m_displayPipeline;

    // 累积双缓冲：本帧读 m_accum[1 - write]、写 m_accum[write]。
    // 样本数到上限后 Trace 会整趟跳过，此时 Display 仍要拿到最后一次写入的那张，
    // 所以另记一个 latest，而不是从 write 反推。
    TitusRHI::TextureHandle      m_accum[2];
    TitusRHI::RenderTargetHandle m_accumRT[2];
    uint32_t                     m_accumWrite = 0;
    uint32_t                     m_accumLatest = 0;
    // 刚建好的累积纹理内容未定义，VK 里布局还是 UNDEFINED——即使 shader 在
    // 首帧不去采样它，把它绑成描述符就已经违反布局要求了。所以第一次 Record
    // 时先给两张都清一遍。
    bool                         m_needsClear = false;

    TitusRHI::SamplerHandle m_pointSampler;

    TitusRHI::BufferHandle m_sceneUbo;
    TitusRHI::BufferHandle m_frameUbo;
    TitusRHI::BufferHandle m_displayUbo;

    // 已累积样本数；0 表示这一帧是重新开始，shader 不读历史。
    uint32_t m_accumulatedSamples = 0;
    // 每帧自增，作为 shader 里 RNG 的帧种子；不参与累积重置判定。
    uint32_t m_frameSeed = 0;

    TitusMath::Mat4 m_prevView{1.0f};
    TitusMath::Mat4 m_prevProj{1.0f};
    bool            m_hasPrevCamera = false;
};
