#pragma once
// ============================================================================
// RendererVK - VKDevice
// 继承 RendererCore::GThreadableDevice（后端无关基类）：
//   - 基类承担参数校验、句柄 ID 分配、模板方法骨架（Init/Shutdown/CreateXxx）
//   - 子类只实现 OnInitBackend / OnInitSwapchain / *Impl() 钩子
//   - 内部聚合 VkContext / VkSwapchainWrapper / VkPassScheduler 等现有低层组件，
//     对外只通过 RendererCore 接口暴露能力
//   - 维护"句柄 ↔ 后端原生对象"的映射表
//   - 把 Fence / Semaphore / vkAcquireNextImageKHR / vkQueueSubmit / vkQueuePresentKHR
//     等同步逻辑全部封装到 BeginFrameImpl / SubmitImpl / PresentImpl 内部
// ============================================================================
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>

#include "RendererCore/GThreadableDevice.h"
#include "VKCommandList.h"

class VkContext;
class VkSwapchainWrapper;
class VkWindow;
class VkCommandBufferWrapper;

namespace TitusVkGraphics
{
    // ------------------------------------------------------------------------
    // 内部记录的资源条目（按类型）
    // ------------------------------------------------------------------------
    struct VKBufferEntry
    {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint64_t       size   = 0;
        VkBufferUsageFlags    usage    = 0;
        VkMemoryPropertyFlags memProps = 0;
        void*          mappedPtr = nullptr;   // CpuToGpu/CpuOnly 时持久映射
    };

    struct VKTextureEntry
    {
        VkImage        image       = VK_NULL_HANDLE;
        VkDeviceMemory memory      = VK_NULL_HANDLE;
        VkImageView    defaultView = VK_NULL_HANDLE;
        VkFormat       format      = VK_FORMAT_UNDEFINED;
        uint32_t       width  = 0;
        uint32_t       height = 0;
        uint32_t       mipLevels   = 1;
        uint32_t       arrayLayers = 1;
        VkImageLayout  currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct VKSamplerEntry
    {
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct VKShaderEntry
    {
        VkShaderModule        module = VK_NULL_HANDLE;
        VkShaderStageFlagBits stage  = VK_SHADER_STAGE_VERTEX_BIT;
        std::string           entry  = "main";
        TitusRHI::ReflectionInfo reflection;
    };

    struct VKPipelineEntry
    {
        VkPipeline             pipeline   = VK_NULL_HANDLE;
        VkPipelineLayout       layout     = VK_NULL_HANDLE;
        // 由 Pipeline 拥有的 DescriptorSetLayout（按 set 索引）
        std::vector<VkDescriptorSetLayout> setLayouts;
        // 标记 Graphics 还是 Compute；BindPipeline / Dispatch 据此派发
        bool                   isCompute  = false;
        VkPipelineBindPoint    bindPoint  = VK_PIPELINE_BIND_POINT_GRAPHICS;
        // 图形管线烘焙时若 rtLayout 与 swapchain 默认 RP 不兼容，
        // 这里持有一个"仅用于 pipeline compatibility"的临时 VkRenderPass。
        // 销毁 pipeline 时一同释放；若引用的是 swapchain 默认 RP 则 owned=false。
        VkRenderPass           compatRenderPass = VK_NULL_HANDLE;
        bool                   ownsCompatRenderPass = false;
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追管线：SBT buffer 与四个 region 的地址/步长，供 TraceRays 使用。
        bool                            isRayTracing = false;
        VkBuffer                        sbtBuffer = VK_NULL_HANDLE;
        VkDeviceMemory                  sbtMemory = VK_NULL_HANDLE;
        VkStridedDeviceAddressRegionKHR raygenRegion{};
        VkStridedDeviceAddressRegionKHR missRegion{};
        VkStridedDeviceAddressRegionKHR hitRegion{};
        VkStridedDeviceAddressRegionKHR callableRegion{};
#endif
    };

    struct VKRenderTargetEntry
    {
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass  renderPass  = VK_NULL_HANDLE;
        VkExtent2D    extent      = { 0, 0 };
        // attachments 引用句柄（不在此处 own）
        std::vector<TitusRHI::TextureHandle> colorAttachments;
        TitusRHI::TextureHandle depthStencilAttachment;
    };

#if defined(RENDERER_ENABLE_RAY_TRACING)
    // ------------------------------------------------------------------------
    // 加速结构条目（光追）
    //   - as：VkAccelerationStructureKHR 本体
    //   - buffer/memory：AS 的 backing storage（ACCELERATION_STRUCTURE_STORAGE usage）
    //   - deviceAddress：TLAS 引用 BLAS / 描述符绑定时使用
    //   - instanceBuffer/instanceMemory：仅 TLAS 使用，存放打包后的
    //     VkAccelerationStructureInstanceKHR[]，生命周期随 AS
    // ------------------------------------------------------------------------
    struct VKAccelStructEntry
    {
        VkAccelerationStructureKHR as     = VK_NULL_HANDLE;
        VkBuffer        buffer            = VK_NULL_HANDLE;
        VkDeviceMemory  memory            = VK_NULL_HANDLE;
        VkDeviceAddress deviceAddress     = 0;
        VkBuffer        instanceBuffer    = VK_NULL_HANDLE; // 仅 TLAS
        VkDeviceMemory  instanceMemory    = VK_NULL_HANDLE; // 仅 TLAS
        VkDeviceAddress instanceBufferAddr = 0;             // 仅 TLAS，供 refit 使用
        TitusRHI::AccelerationStructureType type =
            TitusRHI::AccelerationStructureType::BottomLevel;

        // 动态更新：仅当创建时带 AllowUpdate 标志的 TLAS
        // 才保留以下资源，供命令流内 refit（vkCmdBuildAccelerationStructuresKHR
        // 的 UPDATE 模式）复用。
        bool                        allowUpdate    = false;
        VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
        uint32_t                    instanceCapacity = 0;  // instanceBuffer 可容纳的 instance 数
        VkBuffer                    updateScratch  = VK_NULL_HANDLE; // 持久 scratch（含 build/update 余量）
        VkDeviceMemory              updateScratchMem = VK_NULL_HANDLE;
        VkDeviceAddress             updateScratchAddr = 0; // 已按 scratch 对齐
    };
#endif

    // ------------------------------------------------------------------------
    // 每帧同步对象（Frame in Flight）：完全封装在 VKDevice 内部
    // ------------------------------------------------------------------------
    struct VKFrameSync
    {
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence     inFlightFence  = VK_NULL_HANDLE;
        std::unique_ptr<VkCommandBufferWrapper> primaryCmd;
        // 每帧一个 DescriptorPool；与 CommandList 的 per-frame DS 缓存配套跨帧复用，
        // 仅在 Allocate 失败时整池 reset（见 AllocateDescriptorSet）。
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    };

    // ------------------------------------------------------------------------
    // VKDevice —— 继承 GThreadableDevice 的 Vulkan 子类
    // ------------------------------------------------------------------------
    class VKDevice : public TitusRHI::GThreadableDevice
    {
    public:
        VKDevice();
        ~VKDevice() override;

        VKDevice(const VKDevice&)            = delete;
        VKDevice& operator=(const VKDevice&) = delete;

        // ====================================================================
        // 后端类型（IGDevice 接口）
        // ====================================================================
        TitusRHI::GBackend GetBackend() const override { return TitusRHI::GBackend::Vulkan; }

        // VK 后端自管 VkWindow，主循环 ShouldClose() 需要从这里问询。
        bool IsWindowClosed() const override;

        // VK 后端自管 VkWindow，INPUT_MANAGER 通过此接口拿到 GLFWwindow*。
        void* GetWindowNativeHandle() const override;

        // ImGui Overlay 录制 Hook（详见 IGDevice.h）。
        // VK 后端实现：开 swapchain 默认 RenderPass（loadOp=Load）+
        // 调用注入的 callback（其中会执行 ImGui_ImplVulkan_RenderDrawData
        // 到当前帧的 primary cmdbuf）+ EndRenderPass。
        void SetImGuiOverlayCallback(ImGuiOverlayCallback cb, void* userData) override;
        void RenderImGuiOverlay() override;

        // 暴露当前帧的 primary VkCommandBuffer（仅供 RendererInterface
        // IMGUI 模块在 RenderImGuiOverlay 回调内使用，业务侧绝不应直接使用）。
        VkCommandBuffer GetCurrentPrimaryCommandBuffer() const;

        // one-shot 命令缓冲的对外暴露（imgui 上传 Font 纹理时需要）。
        VkCommandBuffer ImGuiBeginOneTimeCommands();
        void            ImGuiEndOneTimeCommands(VkCommandBuffer cb);

        // 修复：内部辅助——把 imgui draw 录到当前 primaryCmd 上。
        // 在 SubmitImpl 内的 primaryCmd->End() 之前调用。public override
        // RenderImGuiOverlay() 已变成 no-op（PassScheduler 在 Submit *后*
        // 才调，那时 cmdbuf 已 End）。
        void RecordImGuiOverlayInPrimaryCmd();

        // ====================================================================
        // 后端内部访问（仅 VKCommandList 使用，不对业务暴露）
        // ====================================================================
        VkContext*           GetVkContext()    { return m_context.get(); }
        VkSwapchainWrapper*  GetVkSwapchain()  { return m_swapchain.get(); }

        const VKBufferEntry*       LookupBuffer(TitusRHI::BufferHandle h)             const;
        // BindResourceSet 中需要在写入前转换 storage image layout，
        //           因此提供可写访问以更新 currentLayout。
        VKTextureEntry*            MutableLookupTexture(TitusRHI::TextureHandle h);
        const VKTextureEntry*      LookupTexture(TitusRHI::TextureHandle h)           const;
        const VKSamplerEntry*      LookupSampler(TitusRHI::SamplerHandle h)           const;
        const VKShaderEntry*       LookupShader(TitusRHI::ShaderHandle h)             const;
        const VKPipelineEntry*     LookupPipeline(TitusRHI::PipelineHandle h)         const;
        const VKRenderTargetEntry* LookupRenderTarget(TitusRHI::RenderTargetHandle h) const;
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追：供 VKCommandList 绑定 TLAS 描述符使用。
        const VKAccelStructEntry*  LookupAccelStruct(TitusRHI::AccelerationStructureHandle h) const;
#endif

        // 返回当前帧的 swapchain image index（BeginFrame 中已 acquire）
        uint32_t GetCurrentImageIndex() const { return m_currentImageIndex; }

        // 上一完整帧的 BindResourceSet 统计（Submit 时固化；Overlay 排查用）
        DescriptorBindStats GetLastFrameDescriptorBindStats() const
        {
            return m_commandList ? m_commandList->GetLastFrameDescriptorBindStats()
                                 : DescriptorBindStats{};
        }

        // ====================================================================
        // DescriptorSet 分配（仅供 VKCommandList 使用）
        //   每帧一个 DescriptorPool，BeginFrame 时整池 reset。
        //   返回的 VkDescriptorSet 在 SubmitImpl 完成 + 下一次 BeginFrameImpl
        //   到达同一帧之间有效。
        // ====================================================================
        VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout);

    protected:
        // ====================================================================
        // GThreadableDevice / GDevice 钩子
        // ====================================================================
        bool OnInitBackend  (const TitusRHI::GDeviceDesc& desc, TitusRHI::IWindow* window) override;
        bool OnInitSwapchain(TitusRHI::IWindow* window)                                       override;
        void OnShutdownSwapchain()                                                              override;
        void OnShutdownBackend()                                                                override;

        void OnWaitIdleImpl()                                       override;
        void OnWindowResizedImpl(uint32_t width, uint32_t height)   override;

        // 资源 *Impl()：基类已分配 id；子类只关注 VkXxx 创建与映射
        bool CreateBufferImpl      (uint64_t id, const TitusRHI::BufferDesc& desc)            override;
        bool CreateTextureImpl     (uint64_t id, const TitusRHI::TextureDesc& desc)           override;
        bool CreateSamplerImpl     (uint64_t id, const TitusRHI::SamplerDesc& desc)           override;
        bool CreateShaderImpl      (uint64_t id, const TitusRHI::ShaderDesc& desc)            override;
        bool CreatePipelineImpl    (uint64_t id, const TitusRHI::GraphicsPipelineDesc& desc)  override;
        // Compute 管线创建（GDevice 基类已默认返回 false，这里 override 启用）
        bool CreatePipelineImpl    (uint64_t id, const TitusRHI::ComputePipelineDesc&  desc)  override;
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追管线创建：仅在设备支持 RT 管线时成功。
        bool CreatePipelineImpl    (uint64_t id, const TitusRHI::RayTracingPipelineDesc& desc) override;
#endif
        bool CreateRenderTargetImpl(uint64_t id, const TitusRHI::RenderTargetDesc& desc)      override;
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追：加速结构创建/销毁。基类默认返回 false/空实现，
        // 这里 override 启用真正的 VK 实现。
        bool CreateAccelerationStructureImpl(uint64_t id,
                                             const TitusRHI::AccelerationStructureDesc& desc) override;
        void DeleteAccelerationStructureImpl(uint64_t id) override;
#endif

        void DeleteBufferImpl      (uint64_t id) override;
        void DeleteTextureImpl     (uint64_t id) override;
        void DeleteSamplerImpl     (uint64_t id) override;
        void DeleteShaderImpl      (uint64_t id) override;
        void DeletePipelineImpl    (uint64_t id) override;
        void DeleteRenderTargetImpl(uint64_t id) override;

        // 上传 *Impl()
        void UpdateBufferImpl (TitusRHI::BufferHandle  buffer,
                               const void*               src,
                               size_t                    bytes,
                               size_t                    dstOffset) override;
        void UpdateTextureImpl(TitusRHI::TextureHandle texture,
                               const TitusRHI::TextureUploadDesc& upload) override;

        // 帧控制 *Impl()
        void                            BeginFrameImpl()                                       override;
        TitusRHI::RenderCommandList*  AcquireCommandListImpl()                               override;
        void                            SubmitImpl(TitusRHI::RenderCommandList* cmd)         override;
        void                            PresentImpl()                                          override;

    private:
        // 同步对象 / CommandPool 管理
        void CreateCommandPool();
        void CreateSyncObjects(uint32_t framesInFlight);
        void DestroySyncObjects();
        // DescriptorPool 创建与销毁（每帧一个）
        void CreateDescriptorPools(uint32_t framesInFlight);
        void DestroyDescriptorPools();
        // 用 immediate one-shot CmdBuffer 把 image 转 layout
        void TransitionImageLayoutImmediate(VkImage image,
                                            VkImageAspectFlags aspect,
                                            uint32_t mipLevels,
                                            uint32_t arrayLayers,
                                            VkImageLayout oldLayout,
                                            VkImageLayout newLayout);

        // 根据 RenderTargetLayout 构造一个仅用于 graphics pipeline
        // compatibility 的临时 VkRenderPass（attachment count + format + samples
        // 与实际 RT 的 RenderPass 兼容；loadOp / storeOp / layout 不影响兼容性）。
        // 由调用方持有所有权。
        VkRenderPass CreateCompatibleRenderPass(const TitusRHI::RenderTargetLayout& rtLayout);

        // 通用 immediate one-shot CmdBuffer 基础设施。
        // 用于资源初始化期的 staging upload / image layout 转换 / blit 等。
        // 走 graphicsQueue + vkQueueWaitIdle，与 frame in flight 同步无关。
        VkCommandBuffer BeginOneTimeCommands();
        void            EndOneTimeCommands(VkCommandBuffer cb);

        // 把 host 数据通过 staging buffer 拷到 device-local buffer。
        //   1) 创建 host-visible staging buffer + memcpy；
        //   2) one-shot CmdBuffer：vkCmdCopyBuffer(staging → dst)；
        //   3) 释放 staging。
        void UploadBufferViaStaging(VkBuffer dstBuffer,
                                    VkDeviceSize dstOffset,
                                    const void* src,
                                    VkDeviceSize bytes);

        // 把 host 像素数据通过 staging buffer 拷到 image 的指定 subresource。
        //   1) 创建 host-visible staging buffer + memcpy；
        //   2) one-shot CmdBuffer：UNDEFINED→TRANSFER_DST → vkCmdCopyBufferToImage
        //                          → TRANSFER_DST→SHADER_READ_OPTIMAL；
        //   3) 释放 staging。
        // 调用方需保证 image 的 usage 包含 TRANSFER_DST。
        void UploadImageViaStaging(VkImage image,
                                   VkImageAspectFlags aspect,
                                   uint32_t mipLevel,
                                   uint32_t arrayLayer,
                                   uint32_t mipCount,
                                   uint32_t layerCount,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t depth,
                                   const void* src,
                                   VkDeviceSize bytes,
                                   VkImageLayout finalLayout);

        // 内存分配辅助：根据 typeFilter + 属性挑选 memoryType
        void AllocateBufferMemory(VKBufferEntry& entry, VkMemoryPropertyFlags props);

#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追：创建一个裸 VkBuffer + 内存（供 AS backing /
        // scratch / instance buffer 使用）。usage 含 SHADER_DEVICE_ADDRESS 时自动
        // 挂接 VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT。失败返回 false。
        bool CreateRawBuffer(VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags props,
                             VkBuffer& outBuffer,
                             VkDeviceMemory& outMemory);
        // 查询 buffer 的设备地址（要求 buffer usage 含 SHADER_DEVICE_ADDRESS）。
        VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer) const;
#endif

        // 通过 VkContext 查询能力
        void FillCaps();

    private:
        // ------------------------------------------------------------------
        // 底层组件
        // ------------------------------------------------------------------
        std::unique_ptr<VkWindow>           m_internalWindow;   // 当业务未传 IWindow 时由 Device 自建
        VkWindow*                           m_windowPtr = nullptr;
        std::unique_ptr<VkContext>          m_context;
        std::unique_ptr<VkSwapchainWrapper> m_swapchain;

        // ------------------------------------------------------------------
        // Per-frame 同步对象 + Primary CmdBuffer
        // ------------------------------------------------------------------
        VkCommandPool             m_commandPool   = VK_NULL_HANDLE;
        std::vector<VKFrameSync>  m_frames;
        // 每个 swapchain image 独立的 renderFinished semaphore，避免 framesInFlight < imageCount
        // 时同一 semaphore 被两帧复用（VUID-vkQueueSubmit-pSignalSemaphores-00067）。
        std::vector<VkSemaphore>  m_imageRenderFinishedSemaphores;
        uint32_t                  m_currentImageIndex = 0;
        bool                      m_frameInProgress   = false;

        // 当前帧暴露给业务的 RenderCommandList（每帧重置）
        std::unique_ptr<VKCommandList> m_commandList;

        // ------------------------------------------------------------------
        // 资源映射表
        // ------------------------------------------------------------------
        std::unordered_map<uint64_t, VKBufferEntry>       m_buffers;
        std::unordered_map<uint64_t, VKTextureEntry>      m_textures;
        std::unordered_map<uint64_t, VKSamplerEntry>      m_samplers;
        std::unordered_map<uint64_t, VKShaderEntry>       m_shaders;
        std::unordered_map<uint64_t, VKPipelineEntry>     m_pipelines;
        std::unordered_map<uint64_t, VKRenderTargetEntry> m_renderTargets;
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追：加速结构映射表。
        std::unordered_map<uint64_t, VKAccelStructEntry>  m_accelStructs;
#endif

        // Overlay 回调与用户上下文（由 RendererInterface 的 IMGUI 模块注入）
        ImGuiOverlayCallback m_imGuiCallback = nullptr;
        void*                m_imGuiUserData = nullptr;
    };
}
