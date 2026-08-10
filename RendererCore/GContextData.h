#pragma once
// ============================================================================
// RendererCore - GContextData
// "全局图形上下文"快照：ViewMatrix / ProjectionMatrix /
// WorldMatrix / InsideFrame / ActiveColorTargets 等。
// 作为 GDevice 的非虚成员存在；含矩阵堆栈、
// ShaderParameterSet 合并等具体行为。
// ============================================================================
#include <array>
#include <cstdint>
#include <vector>

#include "TitusMath.h"

#include "GHandle.h"
#include "GDescs.h"
#include "ShaderParameterSet.h"

namespace TitusRHI
{
    static constexpr uint32_t kMaxColorTargets = 8;

    // 与 TitusMath::Mat4 合一（列主序 float[16]）。
    using Matrix4x4f = TitusMath::Mat4;

    // ------------------------------------------------------------------------
    // GContextData —— 设备级"渲染上下文"快照
    // ------------------------------------------------------------------------
    struct GContextData
    {
        // 矩阵栈：world/view/projection 以及它们的 Push/Pop 友好堆栈。
        Matrix4x4f viewMatrix       {};
        Matrix4x4f projectionMatrix {};
        Matrix4x4f worldMatrix      {};
        bool       matrixDirty = false;

        std::vector<Matrix4x4f> worldStack;       // PushWorld / PopWorld 使用
        std::vector<Matrix4x4f> viewStack;
        std::vector<Matrix4x4f> projectionStack;

        // 帧状态
        bool     insideFrame      = false;
        uint32_t currentFrameIndex = 0;

        // 当前激活的 RenderTarget 状态
        std::array<RenderTargetHandle, kMaxColorTargets> activeColorTargets {};
        RenderTargetHandle                               activeDepthTarget  {};
        uint32_t                                         activeColorCount   = 0;
        uint32_t                                         activeCubemapFace  = 0;
        uint32_t                                         activeMipLevel     = 0;

        // 当前绑定的管线 / Viewport / Scissor（去重参考）
        PipelineHandle currentPipeline {};
        Viewport       currentViewport {};
        Rect2D         currentScissor  {};

        // 全局 shader 属性表。Material::Apply 会与本表合并后提交到 GPU。
        ShaderParameterSet globalProperties;
    };
}
