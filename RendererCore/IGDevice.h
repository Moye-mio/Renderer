#pragma once
// ============================================================================
// RendererCore - IGDevice
// 后端无关的设备接口：所有方法签名仅使用 RendererCore 自定义 Handle / Desc / Enum。
// 业务代码与后续高层封装（Material / Mesh / Camera）只依赖该接口。
// ============================================================================
#include <cstdint>
#include <vector>

#include "GHandle.h"
#include "GEnums.h"
#include "GDescs.h"

namespace TitusRHI
{
    // 前向声明，避免相互包含
    class IWindow;
    class RenderCommandList;

    // ------------------------------------------------------------------------
    // IGDevice —— 设备接口
    // 分组：生命周期 / 资源创建销毁 / 数据上传 / 帧控制 / 能力查询
    // ------------------------------------------------------------------------
    class IGDevice
    {
    public:
        virtual ~IGDevice() = default;

        // ====================================================================
        // 生命周期
        // ====================================================================
        // 初始化设备：根据 desc.backend 与 window 完成所有底层对象创建
        virtual bool Init(const GDeviceDesc& desc, IWindow* window) = 0;

        // 反初始化：释放全部底层对象。调用后该对象不可再用
        virtual void Shutdown() = 0;

        // 等待 GPU 全部任务完成（VK：vkDeviceWaitIdle；GL：glFinish）
        virtual void WaitIdle() = 0;

        // 通知设备 swapchain 需要重建（窗口尺寸变化）
        virtual void OnWindowResized(uint32_t width, uint32_t height) = 0;

        // ====================================================================
        // 资源创建 / 销毁
        // ====================================================================
        virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
        virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
        virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
        virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;
        virtual PipelineHandle CreatePipeline(const GraphicsPipelineDesc& desc) = 0;
        // 计算管线创建重载（同名不同参数）。
        // 后端可选择与 Graphics PSO 在同一句柄空间中跟踪。
        virtual PipelineHandle CreatePipeline(const ComputePipelineDesc& desc) = 0;
        // 光追管线创建重载。不支持 RT 管线的
        // 后端返回 invalid 句柄（调用方以 IsValid 判定）。
        virtual PipelineHandle CreatePipeline(const RayTracingPipelineDesc& desc) = 0;
        virtual RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc) = 0;
        // 光追：创建加速结构（BLAS/TLAS）。
        // 不支持光追的后端返回 invalid 句柄（调用方以 IsValid 判定）。
        virtual AccelerationStructureHandle CreateAccelerationStructure(const AccelerationStructureDesc& desc) = 0;

        virtual void Destroy(BufferHandle handle) = 0;
        virtual void Destroy(TextureHandle handle) = 0;
        virtual void Destroy(SamplerHandle handle) = 0;
        virtual void Destroy(ShaderHandle handle) = 0;
        virtual void Destroy(PipelineHandle handle) = 0;
        virtual void Destroy(RenderTargetHandle handle) = 0;
        virtual void Destroy(AccelerationStructureHandle handle) = 0;

        // ====================================================================
        // 数据上传
        // ====================================================================
        // 更新 Buffer 部分内容
        virtual void UpdateBuffer(BufferHandle buffer,
                                  const void* src,
                                  size_t bytes,
                                  size_t dstOffset = 0) = 0;

        // 更新 Texture 部分内容
        virtual void UpdateTexture(TextureHandle texture,
                                   const TextureUploadDesc& upload) = 0;

        // ====================================================================
        // 帧控制
        // ====================================================================
        // BeginFrame 内部完成"获取下一帧 swapchain image"等同步动作；多帧 In-Flight 的
        // Fence/Semaphore 完全封装在后端内部，不外泄。
        virtual void BeginFrame() = 0;

        // 取得本帧用于录制命令的 CommandList。返回的指针生命周期由设备管理，
        // 调用方不得 delete；必须在 Submit 后视为失效。
        virtual RenderCommandList* AcquireCommandList() = 0;

        // 提交命令列表至 GPU。可多次调用以提交多个 list（预留并行录制能力）。
        virtual void Submit(RenderCommandList* cmd) = 0;

        // 呈现：把本帧渲染结果显示到窗口
        virtual void Present() = 0;

        // 读回当前 backbuffer / swapchain 图像为紧密 RGBA8（第 0 行为图像顶部）。
        // 调用约定：
        //   - OpenGL：在本帧已绘制到默认 FBO 之后、SwapBuffers 之前调用；
        //   - Vulkan：必须在 Present 之前调用（Present 后 image 归 presentation engine）。
        // 不支持的后端（Null 等）返回 false。
        virtual bool ReadbackBackbuffer(std::vector<uint8_t>& /*outRgba*/,
                                        uint32_t& /*outWidth*/,
                                        uint32_t& /*outHeight*/)
        {
            return false;
        }

        // 当前帧索引（[0, framesInFlight)）；业务侧可用其挑选 per-frame UBO 等
        virtual uint32_t GetCurrentFrameIndex() const = 0;

        // ====================================================================
        // 能力查询（避免业务侧 dynamic_cast）
        // ====================================================================
        virtual GBackend GetBackend() const = 0;
        virtual const GCaps& GetCaps() const = 0;

        // 当后端自管窗口（VK 路径）时，业务侧 g.window 可能为 nullptr，
        // 此时主循环的 ShouldClose() 需要从设备侧问询窗口关闭状态。GL 路径下
        // g.window 由 RendererInterface 管理，IsWindowClosed 默认 false 即可。
        virtual bool IsWindowClosed() const { return false; }

        // 当后端自管窗口时（VK 路径下 m_internalWindow），输入服务（键盘
        // /鼠标）需要通过此接口拿到原生 GLFWwindow* 才能查询。GL 路径下
        // RendererInterface 已经持有 IWindow 句柄，无需经过此接口；默认返回 nullptr。
        // 业务侧绝不应直接使用，仅供 RendererInterface 内部 INPUT_MANAGER 调用。
        virtual void* GetWindowNativeHandle() const { return nullptr; }

        // ImGui Overlay 录制 Hook
        //   - 由 PassScheduler::DrawFrame 在所有 Pass 录制完成后、Submit 之前调用；
        //   - GL 后端：默认 FB 仍绑定（ScreenQuadPass 已结束 RP），直接调用
        //     ImGui_ImplOpenGL3_RenderDrawData 即可；
        //   - VK 后端：开一段 swapchain 默认 RenderPass（loadOp=Load）+
        //     ImGui_ImplVulkan_RenderDrawData(currentCmdBuf) + EndRenderPass；
        //   - 默认空实现：未启用 ImGui 时所有后端零开销。
        //   实际 imgui 调用由 RendererInterface 的 IMGUI 模块在 SetImGuiOverlayCallback
        //   注入；后端只负责"在正确的时机以正确的状态调用回调"。
        using ImGuiOverlayCallback = void (*)(void* userData);
        virtual void SetImGuiOverlayCallback(ImGuiOverlayCallback /*cb*/, void* /*userData*/) {}
        virtual void RenderImGuiOverlay() {}
    };
}
