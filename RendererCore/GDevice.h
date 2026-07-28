#pragma once
// ============================================================================
// RendererCore - GDevice
// 后端无关的"真正的基类"（class 而非纯接口）。
//
// 与原 IGDevice 的关系：
//   - IGDevice 仍然作为"对外可见的最小接口"保留（向后兼容）。
//   - GDevice 继承 IGDevice，并把"参数校验 + 句柄分配 + 流程编排"等通用逻辑
//     落地为基类的非虚 API，子类只实现各自后端强相关的 *Impl() 钩子。
//   - GL/VK 子类之后会改为继承 GThreadableDevice（其继承 GDevice），并改写
//     XxxImpl() 而非直接 override IGDevice::CreateBuffer 等。任务 1 阶段先给出
//     基类骨架与默认实现：GDevice 把 IGDevice 的全部纯虚方法委托给同名
//     XxxImpl()，使得"现有继承自 IGDevice 的 GLDevice / VKDevice"在改造为
//     "继承 GDevice + 实现 *Impl()" 后能够无缝替换。
//
// 设计参考：
//   - 句柄分配器 / 上下文数据 / 延迟销毁队列 / 当前帧索引 + 模板方法骨架
//   - 需求 3.1 / 3.3 / 3.4 / 3.5 / 3.6 / 4.1 / 5.1
// ============================================================================
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "IGDevice.h"
#include "HandleAllocator.h"
#include "GContextData.h"
#include "GResources.h"
#include "GStateCache.h"

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // PendingDestroy —— 延迟销毁队列条目（任务 7 会扩充）
    // 任务 1 阶段保留极简结构，使基类构造期就能编译；In-Flight 资源延迟释放的真正
    // 落地由任务 7 接管。
    // ------------------------------------------------------------------------
    enum class PendingDestroyKind : uint8_t
    {
        Buffer = 0,
        Texture,
        Sampler,
        Shader,
        Pipeline,
        RenderTarget,
        // 光追（任务 2 / 需求 5）：加速结构纳入既有延迟销毁机制。仅追加。
        AccelerationStructure,
    };

    struct PendingDestroyEntry
    {
        PendingDestroyKind kind;
        uint64_t id;
        uint64_t submitFrame; // 入队时的 m_submitFrameCount；entry.submitFrame + framesInFlight <= 当前 → 释放
    };

    // ------------------------------------------------------------------------
    // GDevice —— 后端无关基类
    //   - 共享非虚成员：HandleAllocator / GContextData / PendingDestroyQueue /
    //     CurrentFrameIndex / GCaps / GDeviceDesc / 当前 IWindow 指针
    //   - 模板方法 Init / Shutdown：固定执行顺序，钩子留给子类
    //   - 资源 Create*：基类完成参数校验 + 句柄分配，调用纯虚 Create*Impl()，由
    //     子类返回是否成功（成功时把 id ↔ 后端原生对象的映射存入子类内部表）
    // ------------------------------------------------------------------------
    class GDevice : public IGDevice
    {
    public:
        GDevice();
        ~GDevice() override;

        GDevice(const GDevice&) = delete;
        GDevice& operator=(const GDevice&) = delete;

        // ====================================================================
        // 生命周期 —— 模板方法骨架
        //   ① 参数校验
        //   ② 调用纯虚 OnInitBackend(desc, window)        —— 子类创建 Instance/Device/Context
        //   ③ 调用纯虚 OnInitSwapchain(window)            —— 子类创建 Swapchain / 默认 FBO
        //   ④ 初始化基类侧的句柄表 / 缓存
        //   ⑤ 调用 PostInitBackend(onRenderThread) —— 由 GThreadableDevice 增强
        // 任意阶段失败 → 回滚已分配资源，返回 false。
        // ====================================================================
        bool Init(const GDeviceDesc& desc, IWindow* window) final;
        void Shutdown() final;

        // 默认实现：转发到 OnWaitIdleImpl()。GL 后端会退化为 glFinish。
        void WaitIdle() override;
        void OnWindowResized(uint32_t width, uint32_t height) override;

        // ====================================================================
        // 资源创建 / 销毁 —— 模板方法骨架
        //   基类：参数校验 + 句柄 ID 分配；
        //   子类：实现 Create*Impl(id, desc) 把 id 与后端原生对象关联。
        //   销毁路径：基类压入 m_PendingDestroyQueue，等 frame+framesInFlight 后调用
        //   Delete*Impl(id) 真正释放（任务 7 落地）。
        // 任务 1 阶段：基类把 CreateBuffer 等公共非虚 API 实现委托到 *Impl()，
        // 子类既可选择 override CreateBuffer 直接返回（保持现有写法），也可
        // override Create*Impl()（推荐方式，子类无需关心 id 分配）。
        // ====================================================================
        BufferHandle CreateBuffer(const BufferDesc& desc) override;
        TextureHandle CreateTexture(const TextureDesc& desc) override;
        SamplerHandle CreateSampler(const SamplerDesc& desc) override;
        ShaderHandle CreateShader(const ShaderDesc& desc) override;
        PipelineHandle CreatePipeline(const GraphicsPipelineDesc& desc) override;
        PipelineHandle CreatePipeline(const ComputePipelineDesc& desc) override;
        // 光追管线（P1 路线 B，任务 13）
        PipelineHandle CreatePipeline(const RayTracingPipelineDesc& desc) override;
        RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc) override;
        // 光追（任务 5 / 需求 4、5）
        AccelerationStructureHandle CreateAccelerationStructure(const AccelerationStructureDesc& desc) override;

        void Destroy(BufferHandle handle) override;
        void Destroy(TextureHandle handle) override;
        void Destroy(SamplerHandle handle) override;
        void Destroy(ShaderHandle handle) override;
        void Destroy(PipelineHandle handle) override;
        void Destroy(RenderTargetHandle handle) override;
        void Destroy(AccelerationStructureHandle handle) override;

        // ====================================================================
        // 数据上传：基类只做参数校验，转发到 *Impl()
        // ====================================================================
        void UpdateBuffer(BufferHandle buffer,
                          const void* src,
                          size_t bytes,
                          size_t dstOffset = 0) override;
        void UpdateTexture(TextureHandle texture,
                           const TextureUploadDesc& upload) override;

        // ====================================================================
        // 帧控制 —— 默认实现转发到 *Impl()，子类只需实现 *Impl() 即可
        // ====================================================================
        void BeginFrame() override;
        RenderCommandList* AcquireCommandList() override;
        void Submit(RenderCommandList* cmd) override;
        void Present() override;
        uint32_t GetCurrentFrameIndex() const override { return m_currentFrameIndex; }

        // ====================================================================
        // 能力查询：基类持有 m_Caps，子类在 OnInitBackend 内填充
        // ====================================================================
        const GCaps& GetCaps() const override { return m_caps; }

        // ====================================================================
        // 资源查询（任务 7）：
        //   为 Material / Shader / Pass 提供"通过句柄反查基类资源对象"。
        //   基类在 Create*Impl 返回 true 后自动在内部元数据表中记录一条
        //   RHIBuffer / RHITexture / RHIShader 条目（仅含 desc + handle）；
        //   上层代码只读 desc/handle，不依赖后端原生句柄。子类可选择 override
        //   返回自己的扩展子类指针（M3-9 / M-A 使用）。
        // ====================================================================
        virtual const RHIBuffer* FindBuffer(BufferHandle h) const;
        virtual const RHITexture* FindTexture(TextureHandle h) const;
        virtual const RHIShader* FindShader(ShaderHandle h) const;

    protected:
        // ====================================================================
        // 子类钩子（纯虚或带默认实现）
        // ====================================================================
        // —— 设备创建 / 销毁 —— 子类必须实现
        virtual bool OnInitBackend(const GDeviceDesc& desc, IWindow* window) = 0;
        virtual bool OnInitSwapchain(IWindow* window) = 0;
        virtual void OnShutdownSwapchain() = 0;
        virtual void OnShutdownBackend() = 0;

        // —— 渲染线程感知（由 GThreadableDevice 提供默认空实现） ——
        virtual void PostInitBackend(bool onRenderThread) { (void)onRenderThread; }

        // —— 等待 GPU 空闲：默认行为子类必须落地 —— 
        virtual void OnWaitIdleImpl() = 0;

        // —— 窗口尺寸变化：默认调用 OnShutdownSwapchain + OnInitSwapchain —— 
        virtual void OnWindowResizedImpl(uint32_t width, uint32_t height);

        // —— 资源创建 *Impl()：返回是否成功；id 由基类分配并通过参数传入 —— 
        virtual bool CreateBufferImpl(uint64_t id, const BufferDesc& desc) = 0;
        virtual bool CreateTextureImpl(uint64_t id, const TextureDesc& desc) = 0;
        virtual bool CreateSamplerImpl(uint64_t id, const SamplerDesc& desc) = 0;
        virtual bool CreateShaderImpl(uint64_t id, const ShaderDesc& desc) = 0;
        virtual bool CreatePipelineImpl(uint64_t id, const GraphicsPipelineDesc& desc) = 0;
        // 任务 7：计算管线。提供默认空实现以避免所有现有子类都被迫
        // 同步追加实现；GL/VK 后端接入后会各自 override。
        // 返回 false 表示不支持；调用者应需检查返回句柄的 IsValid 。
        virtual bool CreatePipelineImpl(uint64_t /*id*/, const ComputePipelineDesc& /*desc*/)
        {
            return false;
        }
        // 光追管线钩子（P1 路线 B，任务 13）。默认返回 false（不支持），
        // 未接入 RT 管线的后端（GL/Null，以及仅 ray query 的 VK 设备）无需实现。
        virtual bool CreatePipelineImpl(uint64_t /*id*/, const RayTracingPipelineDesc& /*desc*/)
        {
            return false;
        }

        virtual bool CreateRenderTargetImpl(uint64_t id, const RenderTargetDesc& desc) = 0;

        // 光追（任务 5 / 需求 4、5）：加速结构创建。提供默认实现（返回 false =
        // 不支持），使 GL / Null 等未接入后端无需被迫实现即可编译。VK 后端 override。
        virtual bool CreateAccelerationStructureImpl(uint64_t /*id*/,
                                                     const AccelerationStructureDesc& /*desc*/)
        {
            return false;
        }

        // —— 资源真正销毁 *Impl()：基类在延迟释放成熟时调用 —— 
        virtual void DeleteBufferImpl(uint64_t id) = 0;
        virtual void DeleteTextureImpl(uint64_t id) = 0;
        virtual void DeleteSamplerImpl(uint64_t id) = 0;
        virtual void DeleteShaderImpl(uint64_t id) = 0;
        virtual void DeletePipelineImpl(uint64_t id) = 0;
        virtual void DeleteRenderTargetImpl(uint64_t id) = 0;
        // 光追：加速结构销毁。默认空实现（与创建默认返回 false 配套）。
        virtual void DeleteAccelerationStructureImpl(uint64_t /*id*/) {}

        // —— 上传 *Impl() —— 
        virtual void UpdateBufferImpl(BufferHandle buffer,
                                      const void* src,
                                      size_t bytes,
                                      size_t dstOffset) = 0;
        virtual void UpdateTextureImpl(TextureHandle texture,
                                       const TextureUploadDesc& upload) = 0;

        // —— 帧控制 *Impl() —— 
        virtual void BeginFrameImpl() = 0;
        virtual RenderCommandList* AcquireCommandListImpl() = 0;
        virtual void SubmitImpl(RenderCommandList* cmd) = 0;
        virtual void PresentImpl() = 0;

        // ====================================================================
        // 基类侧延迟销毁（任务 7 落地）：
        //   - Destroy(handle) 将条目入队，记录入队时的 m_currentFrameIndex。
        //   - Present() 在进入下一帧前调用 ProcessPendingDestroysIfReady()：
        //     凡 entry.submitFrame + framesInFlight <= m_currentFrameIndex 的条目
        //     被调用 Delete*Impl(id) 真正释放。
        //   - Shutdown() 会在 OnShutdownBackend 之前强制 Flush 所有残留条目。
        // ====================================================================
        void EnqueueDestroy(PendingDestroyKind kind, uint64_t id);
        void ProcessPendingDestroysIfReady();
        void FlushAllPendingDestroys();

        // ====================================================================
        // 调试 / 校验
        // ====================================================================
        // 渲染线程断言：GThreadableDevice 会重写为真正的线程比对
        virtual void AssertOnRenderThread() const
        {
        }

    protected:
        // ====================================================================
        // 共享成员（句柄分配器 / 上下文数据 / 延迟销毁队列 ...）
        // ====================================================================
        HandleAllocator m_handleAllocator;
        GContextData m_gContextData;
        std::vector<PendingDestroyEntry> m_pendingDestroyQueue;
        uint32_t m_currentFrameIndex = 0; // 在 [0, framesInFlight) 内循环
        uint64_t m_submitFrameCount = 0; // 单调递增；用于延迟销毁判定

        // 任务 7：后端无关元数据表，供 FindBuffer / FindTexture / FindShader 查询。
        // key = handle.id；value = 仅含 desc + handle 的基类包装。
        std::unordered_map<uint64_t, std::unique_ptr<RHIBuffer>> m_bufferRegistry;
        std::unordered_map<uint64_t, std::unique_ptr<RHITexture>> m_textureRegistry;
        std::unordered_map<uint64_t, std::unique_ptr<RHIShader>> m_shaderRegistry;

        // 任务 8：状态对象去重缓存。记录 desc → handle 的反向映射，
        // CreateSampler / CreatePipeline 会先查询本表。
        SamplerCache m_samplerCache;
        PipelineCache m_pipelineCache;

        IWindow* m_window = nullptr;
        GDeviceDesc m_desc{};
        GCaps m_caps{};
        bool m_initialized = false;
    };
}
