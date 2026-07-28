// ============================================================================
// RendererCore - GDevice.cpp
// 模板方法骨架的具体实现：参数校验、句柄分配、调用子类 *Impl()。
// 严格保持后端无关：本文件不得 include 任何 <vulkan/...> / <GL/...> /
// <glad/...> / <glfw3.h>。
// 设计参考：requirements.md 需求 3 / 4.1。
// ============================================================================
#include "GDevice.h"

#include <cassert>
#include <iostream>
#include "Logger.h"

namespace TitusRHI
{
    GDevice::GDevice() = default;
    GDevice::~GDevice() = default;

    // ------------------------------------------------------------------------
    // 生命周期
    // ------------------------------------------------------------------------
    bool GDevice::Init(const GDeviceDesc& desc, IWindow* window)
    {
        // ① 参数校验
        if (desc.backend == GBackend::Unknown)
        {
            LOG_STREAM_ERROR("GDevice") << "Init failed: desc.backend == Unknown";
            return false;
        }
        if (desc.framesInFlight == 0 || desc.framesInFlight > 8)
        {
            LOG_STREAM_ERROR("GDevice") << "Init failed: framesInFlight out of range ("
                << desc.framesInFlight << ")";
            return false;
        }

        m_desc = desc;
        m_window = window;

        // ② 子类后端创建（VkInstance/Device/Queue 或 GLFW makeCurrent + glad）
        if (!OnInitBackend(m_desc, m_window))
        {
            LOG_STREAM_ERROR("GDevice") << "OnInitBackend failed";
            m_desc = {};
            m_window = nullptr;
            return false;
        }

        // ③ 子类 Swapchain 创建（VkSwapchain / 默认 FBO）
        if (!OnInitSwapchain(m_window))
        {
            LOG_STREAM_ERROR("GDevice") << "OnInitSwapchain failed; rolling back backend";
            OnShutdownBackend();
            m_desc = {};
            m_window = nullptr;
            return false;
        }

        // ④ 基类侧句柄表 / 上下文初始化
        m_handleAllocator.Reset();
        m_pendingDestroyQueue.clear();
        m_bufferRegistry.clear();
        m_textureRegistry.clear();
        m_shaderRegistry.clear();
        m_samplerCache.clear();
        m_pipelineCache.clear();
        m_currentFrameIndex = 0;
        m_submitFrameCount = 0;
        m_gContextData = GContextData{};

        // ⑤ 通知子类设备已创建（GThreadableDevice 在此处获取渲染线程归属）
        PostInitBackend(/*onRenderThread=*/false);

        m_initialized = true;
        return true;
    }

    void GDevice::Shutdown()
    {
        if (!m_initialized) return;

        // 任务 7：Shutdown 前强制 Flush 所有延迟销毁条目（在 OnWaitIdle 之后）
        OnWaitIdleImpl();
        FlushAllPendingDestroys();

        // 反向顺序释放
        OnShutdownSwapchain();
        OnShutdownBackend();

        m_pendingDestroyQueue.clear();
        m_window = nullptr;
        m_desc = {};
        m_caps = {};
        m_initialized = false;
    }

    void GDevice::WaitIdle()
    {
        if (!m_initialized) return;
        OnWaitIdleImpl();
    }

    void GDevice::OnWindowResized(uint32_t width, uint32_t height)
    {
        if (!m_initialized) return;
        OnWindowResizedImpl(width, height);
    }

    void GDevice::OnWindowResizedImpl(uint32_t width, uint32_t height)
    {
        // 默认行为：等 GPU 空闲 → 重建 Swapchain
        OnWaitIdleImpl();
        OnShutdownSwapchain();
        if (!OnInitSwapchain(m_window))
        {
            LOG_STREAM_ERROR("GDevice") << "OnWindowResizedImpl: swapchain rebuild failed (" << width << "x" << height << ")";
        }
    }

    // ------------------------------------------------------------------------
    // 资源创建 —— 模板方法：基类分配 id + 子类 *Impl() + 失败回滚
    // ------------------------------------------------------------------------
    BufferHandle GDevice::CreateBuffer(const BufferDesc& desc)
    {
        if (desc.size == 0 || desc.usage == BufferUsage::None)
        {
            LOG_STREAM_ERROR("GDevice") << "CreateBuffer: invalid desc (size=" << desc.size << ")";
            return BufferHandle{};
        }
        const uint64_t id = m_handleAllocator.Allocate();
        if (!CreateBufferImpl(id, desc))
        {
            LOG_STREAM_ERROR("GDevice") << "CreateBufferImpl failed (id=" << id << ")";
            return BufferHandle{};
        }
        // 任务 7：在后端无关元数据表中留下 desc + handle 供 Material / Pass 反查
        BufferHandle h{id};
        m_bufferRegistry.emplace(id, std::make_unique<RHIBuffer>(h, desc));
        return h;
    }

    TextureHandle GDevice::CreateTexture(const TextureDesc& desc)
    {
        if (desc.width == 0 || desc.height == 0)
        {
            LOG_STREAM_ERROR("GDevice") << "CreateTexture: invalid extent";
            return TextureHandle{};
        }
        const uint64_t id = m_handleAllocator.Allocate();
        if (!CreateTextureImpl(id, desc))
        {
            LOG_STREAM_ERROR("GDevice") << "CreateTextureImpl failed (id=" << id << ")";
            return TextureHandle{};
        }
        TextureHandle h{id};
        m_textureRegistry.emplace(id, std::make_unique<RHITexture>(h, desc));
        return h;
    }

    SamplerHandle GDevice::CreateSampler(const SamplerDesc& desc)
    {
        // 任务 8：同 desc 不重复创建 → 查缓存。
        if (auto it = m_samplerCache.find(desc); it != m_samplerCache.end())
        {
            return it->second;
        }
        const uint64_t id = m_handleAllocator.Allocate();
        if (!CreateSamplerImpl(id, desc))
        {
            LOG_STREAM_ERROR("GDevice") << "CreateSamplerImpl failed (id=" << id << ")";
            return SamplerHandle{};
        }
        SamplerHandle h{id};
        m_samplerCache.emplace(desc, h);
        return h;
    }

    ShaderHandle GDevice::CreateShader(const ShaderDesc& desc)
    {
        if (desc.code == nullptr || desc.bytes == 0)
        {
            LOG_STREAM_ERROR("GDevice") << "CreateShader: empty code";
            return ShaderHandle{};
        }
        const uint64_t id = m_handleAllocator.Allocate();
        if (!CreateShaderImpl(id, desc))
        {
            LOG_STREAM_ERROR("GDevice") << "CreateShaderImpl failed (id=" << id << ")";
            return ShaderHandle{};
        }
        ShaderHandle h{id};
        m_shaderRegistry.emplace(id, std::make_unique<RHIShader>(h, desc));
        return h;
    }

    PipelineHandle GDevice::CreatePipeline(const GraphicsPipelineDesc& desc)
    {
        if (!desc.vertexShader.IsValid() || !desc.fragmentShader.IsValid())
        {
            LOG_STREAM_ERROR("GDevice") << "CreatePipeline: vs/fs not set";
            return PipelineHandle{};
        }
        // 任务 8：同 desc 不重复创建 → 查缓存。
        if (auto it = m_pipelineCache.find(desc); it != m_pipelineCache.end())
        {
            return it->second;
        }
        const uint64_t id = m_handleAllocator.Allocate();
        if (!CreatePipelineImpl(id, desc))
        {
            LOG_STREAM_ERROR("GDevice") << "CreatePipelineImpl failed (id=" << id << ")";
            return PipelineHandle{};
        }
        PipelineHandle h{id};
        m_pipelineCache.emplace(desc, h);
        return h;
    }

    PipelineHandle GDevice::CreatePipeline(const ComputePipelineDesc& desc)
    {
        if (!desc.computeShader.IsValid())
        {
            LOG_STREAM_ERROR("GDevice") << "CreatePipeline(Compute): cs not set";
            return PipelineHandle{};
        }
        // 任务 7：Compute 当前不入 PipelineCache（cache key 仅哈希 GraphicsPipelineDesc）。
        // 后续可补一份 ComputePipelineCache；当前直接创建。
        const uint64_t id = m_handleAllocator.Allocate();
        if (!CreatePipelineImpl(id, desc))
        {
            LOG_STREAM_ERROR("GDevice") << "CreatePipelineImpl(Compute) failed or unsupported (id="
                << id << ")";
            return PipelineHandle{};
        }
        return PipelineHandle{id};
    }

    // 光追管线（P1 路线 B，任务 13）：与 Compute 同款骨架，走既有 Pipeline 句柄空间。
    PipelineHandle GDevice::CreatePipeline(const RayTracingPipelineDesc& desc)
    {
        if (desc.stages.empty() || desc.groups.empty())
        {
            LOG_STREAM_ERROR("GDevice") << "CreatePipeline(RayTracing): empty stages/groups";
            return PipelineHandle{};
        }
        const uint64_t id = m_handleAllocator.Allocate();
        if (!CreatePipelineImpl(id, desc))
        {
            LOG_STREAM_ERROR("GDevice") << "CreatePipelineImpl(RayTracing) failed or unsupported (id="
                << id << ")";
            return PipelineHandle{};
        }
        return PipelineHandle{id};
    }

    RenderTargetHandle GDevice::CreateRenderTarget(const RenderTargetDesc& desc)
    {
        if (desc.width == 0 || desc.height == 0)
        {
            LOG_STREAM_ERROR("GDevice") << "CreateRenderTarget: invalid extent";
            return RenderTargetHandle{};
        }
        const uint64_t id = m_handleAllocator.Allocate();
        if (!CreateRenderTargetImpl(id, desc))
        {
            LOG_STREAM_ERROR("GDevice") << "CreateRenderTargetImpl failed (id=" << id << ")";
            return RenderTargetHandle{};
        }
        return RenderTargetHandle{id};
    }

    // 光追（任务 5 / 需求 4、5）：加速结构创建。
    AccelerationStructureHandle GDevice::CreateAccelerationStructure(const AccelerationStructureDesc& desc)
    {
        // 参数校验：BLAS 需至少一个几何；TLAS 需至少一个 instance。
        const bool isBlas = desc.type == AccelerationStructureType::BottomLevel;
        if ((isBlas && desc.geometries.empty()) || (!isBlas && desc.instances.empty()))
        {
            LOG_STREAM_ERROR("GDevice") << "CreateAccelerationStructure: empty "
                << (isBlas ? "geometries" : "instances");
            return AccelerationStructureHandle{};
        }
        const uint64_t id = m_handleAllocator.Allocate();
        if (!CreateAccelerationStructureImpl(id, desc))
        {
            // 后端不支持光追或创建失败：不视为致命错误，返回 invalid 句柄由调用方判定。
            LOG_STREAM_ERROR("GDevice") << "CreateAccelerationStructureImpl failed or unsupported (id="
                << id << ")";
            return AccelerationStructureHandle{};
        }
        return AccelerationStructureHandle{id};
    }

    // ------------------------------------------------------------------------
    // 销毁 —— 推入延迟队列；当前阶段直接调用 Delete*Impl()
    // ------------------------------------------------------------------------
    void GDevice::Destroy(BufferHandle h) { if (h.IsValid()) EnqueueDestroy(PendingDestroyKind::Buffer, h.id); }
    void GDevice::Destroy(TextureHandle h) { if (h.IsValid()) EnqueueDestroy(PendingDestroyKind::Texture, h.id); }
    void GDevice::Destroy(SamplerHandle h) { if (h.IsValid()) EnqueueDestroy(PendingDestroyKind::Sampler, h.id); }
    void GDevice::Destroy(ShaderHandle h) { if (h.IsValid()) EnqueueDestroy(PendingDestroyKind::Shader, h.id); }
    void GDevice::Destroy(PipelineHandle h) { if (h.IsValid()) EnqueueDestroy(PendingDestroyKind::Pipeline, h.id); }
    void GDevice::Destroy(RenderTargetHandle h) { if (h.IsValid()) EnqueueDestroy(PendingDestroyKind::RenderTarget, h.id); }
    void GDevice::Destroy(AccelerationStructureHandle h) { if (h.IsValid()) EnqueueDestroy(PendingDestroyKind::AccelerationStructure, h.id); }

    void GDevice::EnqueueDestroy(PendingDestroyKind kind, uint64_t id)
    {
        // 先从元数据表中移除该句柄的记录，避免后续查询返回已 Destroy 的对象。
        switch (kind)
        {
        case PendingDestroyKind::Buffer: m_bufferRegistry.erase(id);
            break;
        case PendingDestroyKind::Texture: m_textureRegistry.erase(id);
            break;
        case PendingDestroyKind::Shader: m_shaderRegistry.erase(id);
            break;
        case PendingDestroyKind::Sampler:
            {
                // 任务 8：从 SamplerCache 中移除反向映射。由于 cache 以 desc 为键，
                // 需线性扫描找到该 id 的条目。考虑到 sampler 总量较少，可接受。
                for (auto it = m_samplerCache.begin(); it != m_samplerCache.end(); ++it)
                    if (it->second.id == id)
                    {
                        m_samplerCache.erase(it);
                        break;
                    }
                break;
            }
        case PendingDestroyKind::Pipeline:
            {
                for (auto it = m_pipelineCache.begin(); it != m_pipelineCache.end(); ++it)
                    if (it->second.id == id)
                    {
                        m_pipelineCache.erase(it);
                        break;
                    }
                break;
            }
        default: break;
        }

        // 任务 7：真正的延迟销毁 —— 入队，等 entry.submitFrame + framesInFlight <=
        // m_submitFrameCount 后（即 GPU 不可能再读该资源）才调用 Delete*Impl。
        // 如果设备尚未 Init（m_initialized==false）或 framesInFlight==0，退化为立即销毁。
        if (!m_initialized || m_desc.framesInFlight == 0)
        {
            switch (kind)
            {
            case PendingDestroyKind::Buffer: DeleteBufferImpl(id);
                break;
            case PendingDestroyKind::Texture: DeleteTextureImpl(id);
                break;
            case PendingDestroyKind::Sampler: DeleteSamplerImpl(id);
                break;
            case PendingDestroyKind::Shader: DeleteShaderImpl(id);
                break;
            case PendingDestroyKind::Pipeline: DeletePipelineImpl(id);
                break;
            case PendingDestroyKind::RenderTarget: DeleteRenderTargetImpl(id);
                break;
            case PendingDestroyKind::AccelerationStructure: DeleteAccelerationStructureImpl(id);
                break;
            }
            return;
        }
        m_pendingDestroyQueue.push_back(PendingDestroyEntry{kind, id, m_submitFrameCount});
    }

    void GDevice::ProcessPendingDestroysIfReady()
    {
        if (m_pendingDestroyQueue.empty()) return;
        const uint64_t fif = m_desc.framesInFlight ? m_desc.framesInFlight : 1u;
        // 从前向后扫描，以 swap-and-pop 方式在原地删除
        size_t i = 0;
        while (i < m_pendingDestroyQueue.size())
        {
            const PendingDestroyEntry& e = m_pendingDestroyQueue[i];
            if (e.submitFrame + fif <= m_submitFrameCount)
            {
                switch (e.kind)
                {
                case PendingDestroyKind::Buffer: DeleteBufferImpl(e.id);
                    break;
                case PendingDestroyKind::Texture: DeleteTextureImpl(e.id);
                    break;
                case PendingDestroyKind::Sampler: DeleteSamplerImpl(e.id);
                    break;
                case PendingDestroyKind::Shader: DeleteShaderImpl(e.id);
                    break;
                case PendingDestroyKind::Pipeline: DeletePipelineImpl(e.id);
                    break;
                case PendingDestroyKind::RenderTarget: DeleteRenderTargetImpl(e.id);
                    break;
                case PendingDestroyKind::AccelerationStructure: DeleteAccelerationStructureImpl(e.id);
                    break;
                }
                // swap-and-pop
                if (i + 1 < m_pendingDestroyQueue.size())
                    m_pendingDestroyQueue[i] = m_pendingDestroyQueue.back();
                m_pendingDestroyQueue.pop_back();
            }
            else
            {
                ++i;
            }
        }
    }

    void GDevice::FlushAllPendingDestroys()
    {
        for (const auto& e : m_pendingDestroyQueue)
        {
            switch (e.kind)
            {
            case PendingDestroyKind::Buffer: DeleteBufferImpl(e.id);
                break;
            case PendingDestroyKind::Texture: DeleteTextureImpl(e.id);
                break;
            case PendingDestroyKind::Sampler: DeleteSamplerImpl(e.id);
                break;
            case PendingDestroyKind::Shader: DeleteShaderImpl(e.id);
                break;
            case PendingDestroyKind::Pipeline: DeletePipelineImpl(e.id);
                break;
            case PendingDestroyKind::RenderTarget: DeleteRenderTargetImpl(e.id);
                break;
            case PendingDestroyKind::AccelerationStructure: DeleteAccelerationStructureImpl(e.id);
                break;
            }
        }
        m_pendingDestroyQueue.clear();
        m_bufferRegistry.clear();
        m_textureRegistry.clear();
        m_shaderRegistry.clear();
        m_samplerCache.clear();
        m_pipelineCache.clear();
    }

    // ------------------------------------------------------------------------
    // 资源查询（任务 7）
    // ------------------------------------------------------------------------
    const RHIBuffer* GDevice::FindBuffer(BufferHandle h) const
    {
        if (!h.IsValid()) return nullptr;
        auto it = m_bufferRegistry.find(h.id);
        return it == m_bufferRegistry.end() ? nullptr : it->second.get();
    }

    const RHITexture* GDevice::FindTexture(TextureHandle h) const
    {
        if (!h.IsValid()) return nullptr;
        auto it = m_textureRegistry.find(h.id);
        return it == m_textureRegistry.end() ? nullptr : it->second.get();
    }

    const RHIShader* GDevice::FindShader(ShaderHandle h) const
    {
        if (!h.IsValid()) return nullptr;
        auto it = m_shaderRegistry.find(h.id);
        return it == m_shaderRegistry.end() ? nullptr : it->second.get();
    }

    // ------------------------------------------------------------------------
    // 数据上传 —— 参数校验 + 转发
    // ------------------------------------------------------------------------
    void GDevice::UpdateBuffer(BufferHandle buffer, const void* src, size_t bytes, size_t dstOffset)
    {
        if (!buffer.IsValid() || src == nullptr || bytes == 0) return;
        UpdateBufferImpl(buffer, src, bytes, dstOffset);
    }

    void GDevice::UpdateTexture(TextureHandle texture, const TextureUploadDesc& upload)
    {
        if (!texture.IsValid() || upload.data == nullptr || upload.bytes == 0) return;
        UpdateTextureImpl(texture, upload);
    }

    // ------------------------------------------------------------------------
    // 帧控制 —— 转发 + 维护 GContextData::insideFrame / currentFrameIndex
    // ------------------------------------------------------------------------
    void GDevice::BeginFrame()
    {
        BeginFrameImpl();
        m_gContextData.insideFrame = true;
        m_gContextData.currentFrameIndex = m_currentFrameIndex;
    }

    RenderCommandList* GDevice::AcquireCommandList()
    {
        return AcquireCommandListImpl();
    }

    void GDevice::Submit(RenderCommandList* cmd)
    {
        SubmitImpl(cmd);
    }

    void GDevice::Present()
    {
        PresentImpl();
        m_gContextData.insideFrame = false;
        m_currentFrameIndex = (m_currentFrameIndex + 1) % (m_desc.framesInFlight ? m_desc.framesInFlight : 1);
        ++m_submitFrameCount;
        // 任务 7：下一帧开始前处理已成熟的延迟销毁条目
        ProcessPendingDestroysIfReady();
    }
}
