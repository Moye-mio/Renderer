#pragma once
// ============================================================================
// RendererCore - RenderCommandList
// 后端无关的命令录制接口。后端各自把每条接口翻译为 vkCmdXxx 或 glXxx：
//   - VK 后端：内部持有一个 VkCommandBuffer，BeginRenderPass 翻译为 vkCmdBeginRenderPass
//   - GL 后端：内部复用 RenderCommandBuffer 的 std::function 延迟队列；
//                BeginRenderPass = "BindFBO + 按 LoadOp 选择性 glClear + 按 StoreOp
//                选择性 glInvalidateFramebuffer"
// 设计参考：RendererCore 设计方案 §3.3、需求 4。
// ============================================================================
#include <cstdint>

#include "GHandle.h"
#include "GEnums.h"
#include "GDescs.h"

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // AccelerationStructureBuildInfo（光追，任务 6 / 需求 6.1）
    //   - 承载一次命令流内 BLAS/TLAS 构建或 refit 所需的几何 / instance 引用。
    //   - 字段与 AccelerationStructureDesc 复用（BLASGeometryDesc / TLASInstanceDesc /
    //     ASBuildFlags 均定义于 GDescs.h），额外携带 refit 相关信息。
    //   - 全部字段仅使用 RendererCore 自定义句柄/枚举/POD，禁止出现 VkXxx。
    //   - P0 主路径在 CreateAccelerationStructure 内即完成首次构建；本结构用于
    //     动态场景下的重建 / 增量更新（需求 15 的能力预留）。
    // ------------------------------------------------------------------------
    struct AccelerationStructureBuildInfo
    {
        AccelerationStructureType type = AccelerationStructureType::BottomLevel;
        ASBuildFlags buildFlags = ASBuildFlags::PreferFastTrace;

        // type == BottomLevel 时使用
        std::vector<BLASGeometryDesc> geometries;

        // type == TopLevel 时使用
        std::vector<TLASInstanceDesc> instances;

        // refit（增量更新）：true 时按 update 模式构建，依赖创建期的
        // ASBuildFlags::AllowUpdate；source 为被更新的源 AS（可与目标相同）。
        bool update = false;
        AccelerationStructureHandle source;
    };

    // ------------------------------------------------------------------------
    // RenderCommandList —— 命令录制接口
    // 上层调用 Draw / DrawIndexed 时不依赖具体后端，行为完全等价。
    // ------------------------------------------------------------------------
    class RenderCommandList
    {
    public:
        virtual ~RenderCommandList() = default;

        // ====================================================================
        // RenderPass 控制
        // ====================================================================
        virtual void BeginRenderPass(const RenderPassBeginInfo& info) = 0;
        virtual void EndRenderPass() = 0;

        // ====================================================================
        // 视口 / 裁剪
        // ====================================================================
        virtual void SetViewport(const Viewport& viewport) = 0;
        virtual void SetScissor(const Rect2D& scissor) = 0;

        // ====================================================================
        // 资源绑定
        // ====================================================================
        virtual void BindPipeline(PipelineHandle pipeline) = 0;
        virtual void BindVertexBuffer(uint32_t slot,
                                      BufferHandle buffer,
                                      uint64_t offset = 0) = 0;
        virtual void BindIndexBuffer(BufferHandle buffer,
                                     IndexType indexType,
                                     uint64_t offset = 0) = 0;

        // 绑定一个资源集（UBO / Texture / Sampler）。
        // setIndex 的语义对齐 Vulkan descriptor set；GL 后端按 reflection 表把它
        // 映射回 GL 的具体 slot。
        virtual void BindResourceSet(uint32_t setIndex,
                                     const ResourceSetDesc& setDesc) = 0;

        // 推送常量（VK：vkCmdPushConstants；GL：转 glUniformXxx）
        virtual void PushConstants(ShaderStage stages,
                                   uint32_t offset,
                                   uint32_t size,
                                   const void* data) = 0;

        // ====================================================================
        // 绘制
        // ====================================================================
        virtual void Draw(uint32_t vertexCount,
                          uint32_t instanceCount = 1,
                          uint32_t firstVertex = 0,
                          uint32_t firstInstance = 0) = 0;

        virtual void DrawIndexed(uint32_t indexCount,
                                 uint32_t instanceCount = 1,
                                 uint32_t firstIndex = 0,
                                 int32_t vertexOffset = 0,
                                 uint32_t firstInstance = 0) = 0;

        // ====================================================================
        // 计算（任务 7 / M2-A）
        // ====================================================================
        // 调度计算 Pass。要求当前已 BindPipeline(computePipeline)，并已通过
        // BindResourceSet / PushConstants 绑好输入。GL 后端：glDispatchCompute；
        // VK 后端：vkCmdDispatch。
        // 默认实现为空：现有 RenderCommandList 子类（GLCommandList / VKCommandList /
        // GDeviceHeadless / GDeviceMainThread）在阶段 2 接入时各自 override。
        virtual void Dispatch(uint32_t /*groupCountX*/,
                              uint32_t /*groupCountY*/,
                              uint32_t /*groupCountZ*/)
        {
        }

        // 流水线屏障：声明前后访问依赖（含 storage image layout 转换）。
        // GL 后端把 srcStage/dstStage 翻译为 glMemoryBarrier 的位掩码组合；
        // VK 后端 vkCmdPipelineBarrier。
        // 默认实现为空，子类按需 override。
        virtual void PipelineBarrier(const PipelineBarrierDesc& /*desc*/)
        {
        }

        // ====================================================================
        // 加速结构构建（光追，任务 6 / 需求 6.1、6.2）
        // ====================================================================
        // 在命令流中录制 BLAS/TLAS 的构建或 refit。目标 AS 由 handle 指定，
        // 本次构建的几何 / instance 由 info 承载。
        // 默认实现为空：现有 RenderCommandList 子类（GLCommandList / VKCommandList /
        // GDeviceHeadless / GDeviceMainThread）无需被迫实现即可编译，仅支持光追的
        // VK 后端 override。构建完成后需着色器读取时，上层用 PipelineBarrier
        // 表达「AccelerationStructureBuild 写 → shader 读」依赖。
        virtual void BuildAccelerationStructure(AccelerationStructureHandle /*target*/,
                                                const AccelerationStructureBuildInfo& /*info*/)
        {
        }

        // ====================================================================
        // 光线追踪派发（光追管线 / 路线 B，任务 15 / 需求 6.4）
        // ====================================================================
        // 发射 width*height*depth 条光线。要求当前已 BindPipeline(rtPipeline)，
        // 并已通过 BindResourceSet 绑好 TLAS / 输出图像等。VK 后端翻译为
        // vkCmdTraceRaysKHR，使用绑定 RT 管线关联的 raygen/miss/hit/callable
        // 四个 SBT region。默认空实现，仅支持 RT 管线的后端 override。
        virtual void TraceRays(uint32_t /*width*/, uint32_t /*height*/, uint32_t /*depth*/)
        {
        }
    };
}
