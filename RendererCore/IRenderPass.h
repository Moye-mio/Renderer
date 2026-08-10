#pragma once
// ============================================================================
// RendererCore - IRenderPass
// 后端无关的 Pass 基类。虚方法只接收 IGDevice& 与 RenderCommandList&，
// 不再依赖 Renderer/IRenderPass.h 与 RendererVK/IVkRenderPass.h。
// 业务 Pass 一旦继承本类，同一份源代码即可在两个后端复用。
// ============================================================================
#include <cstdint>

namespace TitusRHI
{
    class IGDevice;
    class RenderCommandList;

    // ------------------------------------------------------------------------
    // ERenderPassEvent —— 与原 Renderer/RendererVK 中保持完全一致的事件枚举，
    // 用于 PassScheduler 排序。从 RendererCore 集中持有，避免双份定义。
    // ------------------------------------------------------------------------
    enum class ERenderPassEvent : int
    {
        BeforeRendering       = 0,
        BeforeShadowMap       = 50,
        ShadowMap             = 75,
        AfterShadowMap        = 100,
        BeforeGBuffer         = 150,
        GBuffer               = 175,
        AfterGBuffer          = 200,
        BeforeLighting        = 250,
        Lighting              = 275,
        AfterLighting         = 300,
        BeforeOpaqueShading   = 350,
        OpaqueShading         = 375,
        AfterOpaqueShading    = 400,
        BeforeTransparent     = 450,
        Transparent           = 475,
        AfterTransparent      = 500,
        BeforePostProcess     = 550,
        PostProcess           = 575,
        AfterPostProcess      = 600,
        BeforeFinalBlit       = 650,
        FinalBlit             = 675,
        AfterRendering        = 700,
    };

    // ------------------------------------------------------------------------
    // IRenderPass —— 业务 Pass 基类
    //  - Init/Destroy 只接收 IGDevice
    //  - Update/Record 通过 IGDevice + RenderCommandList 完成所有 GPU 交互
    // ------------------------------------------------------------------------
    class IRenderPass
    {
    public:
        virtual ~IRenderPass() = default;

        // 初始化：在此处创建 Pipeline / Buffer / Texture / Sampler 等资源
        virtual void Init(IGDevice& device) = 0;

        // 反初始化：释放所有 Init 中创建的资源
        virtual void Destroy(IGDevice& device) = 0;

        // 每帧逻辑更新（更新 UBO / 改变绑定等）。默认空实现，业务可不重写。
        virtual void Update(IGDevice& /*device*/, uint32_t /*frameIndex*/) {}

        // 每帧命令录制：业务在 cmd 上记录绘制命令
        virtual void Record(IGDevice&        device,
                            RenderCommandList& cmd,
                            uint32_t           frameIndex,
                            uint32_t           imageIndex) = 0;

        // 排序事件（PassScheduler 按此值排序）
        ERenderPassEvent passEvent = ERenderPassEvent::BeforeRendering;
    };
}
