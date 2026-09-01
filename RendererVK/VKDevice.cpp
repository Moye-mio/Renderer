// ============================================================================
// RendererVK - VKDevice.cpp
// 继承 GThreadableDevice 后的子类实现：
//   - 基类负责参数校验、句柄分配、模板方法骨架（Init/Shutdown/CreateXxx）
//   - 本文件只实现 OnInitBackend / OnInitSwapchain / *Impl() 钩子
//   - 把 VkInstance/Device/Queue/Swapchain/CommandPool/Sync 等同步细节封装
//     在内部，对外只暴露后端无关接口
// ============================================================================
#include "VKDevice.h"
#include "VKCommandList.h"
#include "VKTranslate.h"
#include "VKShaderCompiler.h"

#include "VkContext.h"
#include "VkSwapchainWrapper.h"
#include "VkWindow.h"
#include "VkCommandBufferWrapper.h"
#include "Common.h"

#include "RendererCore/IWindow.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include "Logger.h"
#include "TracySupport.h"
#include <algorithm>

namespace TitusVkGraphics
{
    using namespace TitusRHI;

    // ------------------------------------------------------------------------
    // 构造 / 析构
    // ------------------------------------------------------------------------
    VKDevice::VKDevice() = default;

    VKDevice::~VKDevice()
    {
        // 防御式：若业务忘记 Shutdown 也能清理
        if (m_initialized) Shutdown();
    }

    // ------------------------------------------------------------------------
    // OnInitBackend：创建 VkWindow / VkContext / CommandPool（不含 Swapchain）
    // ------------------------------------------------------------------------
    bool VKDevice::OnInitBackend(const GDeviceDesc& desc, IWindow* /*window*/)
    {
        // 无视上层传来的 IWindow*。RendererInterface 在 VK 模式下不会创建
        // GLFWWindow（参见 InitApp），且 VKDevice 期望的 native handle 是 VkWindow*，
        // 与 GLFWWindow 类型不兼容（之前的强转 static_cast<VkWindow*>(GetNativeHandle())
        // 是 type-confusion bug，会在 device.Init 阶段触发 0xC0000005）。
        // 直接自建 VkWindow 完整管理：glfwInit + glfwCreateWindow + 输入回调。

        // 必须在 VkContext::Init（创建 Instance / Device）之前写入：
        // VkContext 全程读 COMPONENT_CONFIG::ENABLE_VALIDATION_LAYER。
        TitusVkGraphics::COMPONENT_CONFIG::ENABLE_VALIDATION_LAYER = desc.enableValidation;
        LOG_STREAM_INFO("VKDevice")
            << "ENABLE_VALIDATION_LAYER = "
            << (desc.enableValidation ? "on" : "off");

        // 同步业务侧窗口尺寸到 VkWindow 使用的全局变量（默认 1280x720 → 业务配置值）
        if (desc.windowWidth  > 0) TitusVkGraphics::WINDOW_KEYWORD::WINDOW_WIDTH  = static_cast<int>(desc.windowWidth);
        if (desc.windowHeight > 0) TitusVkGraphics::WINDOW_KEYWORD::WINDOW_HEIGHT = static_cast<int>(desc.windowHeight);
        if (desc.applicationName) TitusVkGraphics::WINDOW_KEYWORD::WINDOW_TITLE  = desc.applicationName;

        m_internalWindow = std::make_unique<VkWindow>();
        m_internalWindow->Init();
        m_windowPtr = m_internalWindow.get();

        // 2) 初始化 VkContext（VkInstance / PhysicalDevice / Device / Queues / Surface）
        m_context = std::make_unique<VkContext>();
        m_context->Init(*m_windowPtr);

        // 3) CommandPool（在 Swapchain 之前创建，因为 SyncObjects 需要 PrimaryCmd）
        CreateCommandPool();

        // 4) 填充能力
        FillCaps();
        return true;
    }

    bool VKDevice::OnInitSwapchain(IWindow* /*window*/)
    {
        if (!m_context || !m_windowPtr)
        {
            LOG_STREAM_ERROR("VKDevice") << "OnInitSwapchain: context/window not ready";
            return false;
        }

        // 1) Swapchain（VkSwapchainKHR / ImageViews / Default RenderPass / Framebuffers）
        m_swapchain = std::make_unique<VkSwapchainWrapper>();
        m_swapchain->Init(*m_context, *m_windowPtr);

        // 2) Per-frame 同步对象（SyncObjects 数量 = framesInFlight）
        const uint32_t fif = std::max(1u, m_desc.framesInFlight);
        CreateSyncObjects(fif);

        // 3) 每帧一个 DescriptorPool
        CreateDescriptorPools(fif);

        // 4) 准备一个供业务录制的 RenderCommandList
        m_commandList = std::make_unique<VKCommandList>(this);
        return true;
    }

    void VKDevice::OnShutdownSwapchain()
    {
        if (!m_context) return;
        vkDeviceWaitIdle(m_context->GetDevice());

        DestroyDescriptorPools();
        DestroySyncObjects();
        if (m_swapchain) { m_swapchain->Destroy(*m_context); m_swapchain.reset(); }
        m_commandList.reset();
    }

    void VKDevice::OnShutdownBackend()
    {
        if (!m_context) return;
        vkDeviceWaitIdle(m_context->GetDevice());

#if defined(RENDERER_ENABLE_RAY_TRACING)
        // —— 光追：释放业务侧未显式销毁的加速结构 ——
        for (auto& kv : m_accelStructs)
        {
            auto& e = kv.second;
            if (e.as)              m_context->RT().vkDestroyAccelerationStructureKHR(m_context->GetDevice(), e.as, nullptr);
            if (e.buffer)          vkDestroyBuffer(m_context->GetDevice(), e.buffer, nullptr);
            if (e.memory)          vkFreeMemory(m_context->GetDevice(), e.memory, nullptr);
            if (e.instanceBuffer)  vkDestroyBuffer(m_context->GetDevice(), e.instanceBuffer, nullptr);
            if (e.instanceMemory)  vkFreeMemory(m_context->GetDevice(), e.instanceMemory, nullptr);
            if (e.updateScratch)   vkDestroyBuffer(m_context->GetDevice(), e.updateScratch, nullptr);
            if (e.updateScratchMem)vkFreeMemory(m_context->GetDevice(), e.updateScratchMem, nullptr);
        }
        m_accelStructs.clear();
#endif

        // —— 释放业务侧未显式销毁的资源 ——
        for (auto& kv : m_pipelines)
        {
            if (kv.second.pipeline) vkDestroyPipeline(m_context->GetDevice(), kv.second.pipeline, nullptr);
            if (kv.second.layout)   vkDestroyPipelineLayout(m_context->GetDevice(), kv.second.layout, nullptr);
            for (auto& sl : kv.second.setLayouts)
                if (sl) vkDestroyDescriptorSetLayout(m_context->GetDevice(), sl, nullptr);
            // 释放 pipeline 自有 compat RenderPass（不释放 swapchain 默认 RP）
            if (kv.second.ownsCompatRenderPass && kv.second.compatRenderPass)
                vkDestroyRenderPass(m_context->GetDevice(), kv.second.compatRenderPass, nullptr);
#if defined(RENDERER_ENABLE_RAY_TRACING)
            if (kv.second.sbtBuffer) vkDestroyBuffer(m_context->GetDevice(), kv.second.sbtBuffer, nullptr);
            if (kv.second.sbtMemory) vkFreeMemory(m_context->GetDevice(), kv.second.sbtMemory, nullptr);
#endif
        }
        m_pipelines.clear();

        for (auto& kv : m_shaders)
            if (kv.second.module) vkDestroyShaderModule(m_context->GetDevice(), kv.second.module, nullptr);
        m_shaders.clear();

        for (auto& kv : m_samplers)
            if (kv.second.sampler) vkDestroySampler(m_context->GetDevice(), kv.second.sampler, nullptr);
        m_samplers.clear();

        for (auto& kv : m_renderTargets)
        {
            if (kv.second.framebuffer) vkDestroyFramebuffer(m_context->GetDevice(), kv.second.framebuffer, nullptr);
            if (kv.second.renderPass)  vkDestroyRenderPass (m_context->GetDevice(), kv.second.renderPass,  nullptr);
        }
        m_renderTargets.clear();

        for (auto& kv : m_textures)
        {
            if (kv.second.defaultView) vkDestroyImageView(m_context->GetDevice(), kv.second.defaultView, nullptr);
            if (kv.second.image)       vkDestroyImage    (m_context->GetDevice(), kv.second.image, nullptr);
            if (kv.second.memory)      vkFreeMemory      (m_context->GetDevice(), kv.second.memory, nullptr);
        }
        m_textures.clear();

        for (auto& kv : m_buffers)
        {
            if (kv.second.mappedPtr && kv.second.memory)
                vkUnmapMemory(m_context->GetDevice(), kv.second.memory);
            if (kv.second.buffer) vkDestroyBuffer(m_context->GetDevice(), kv.second.buffer, nullptr);
            if (kv.second.memory) vkFreeMemory   (m_context->GetDevice(), kv.second.memory, nullptr);
        }
        m_buffers.clear();

        // —— CommandPool / Context / Window ——
        if (m_commandPool)
        {
            vkDestroyCommandPool(m_context->GetDevice(), m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }
        if (m_context) { m_context->Destroy(); m_context.reset(); }
        if (m_internalWindow) { m_internalWindow->Terminate(); m_internalWindow.reset(); }
        m_windowPtr = nullptr;
    }

    void VKDevice::OnWaitIdleImpl()
    {
        if (m_context) vkDeviceWaitIdle(m_context->GetDevice());
    }

    void VKDevice::OnWindowResizedImpl(uint32_t /*w*/, uint32_t /*h*/)
    {
        if (m_context && m_swapchain && m_windowPtr)
        {
            vkDeviceWaitIdle(m_context->GetDevice());
            m_swapchain->Recreate(*m_context, *m_windowPtr);
        }
    }

    // ------------------------------------------------------------------------
    // CommandPool / Sync
    // ------------------------------------------------------------------------
    void VKDevice::CreateCommandPool()
    {
        VkCommandPoolCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = m_context->GetQueueFamilyIndices().graphicsFamily;
        VK_CHECK(vkCreateCommandPool(m_context->GetDevice(), &ci, nullptr, &m_commandPool));
    }

    void VKDevice::CreateSyncObjects(uint32_t framesInFlight)
    {
        m_frames.resize(framesInFlight);

        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < framesInFlight; ++i)
        {
            VK_CHECK(vkCreateSemaphore(m_context->GetDevice(), &si, nullptr, &m_frames[i].imageAvailable));
            // renderFinished 已移到 per-image semaphore 数组，这里置空
            m_frames[i].renderFinished = VK_NULL_HANDLE;
            VK_CHECK(vkCreateFence    (m_context->GetDevice(), &fi, nullptr, &m_frames[i].inFlightFence));
            m_frames[i].primaryCmd = std::make_unique<VkCommandBufferWrapper>();
            m_frames[i].primaryCmd->Init(*m_context, m_commandPool,
                                        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                        "VKDevice_PrimaryCmd_" + std::to_string(i));
        }

        // 每个 swapchain image 独立一个 renderFinished semaphore，避免 framesInFlight < imageCount
        // 时同一 semaphore 被两帧复用（VUID-vkQueueSubmit-pSignalSemaphores-00067）。
        const uint32_t imageCount = m_swapchain ? static_cast<uint32_t>(m_swapchain->GetImageCount()) : framesInFlight;
        m_imageRenderFinishedSemaphores.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            VK_CHECK(vkCreateSemaphore(m_context->GetDevice(), &si, nullptr, &m_imageRenderFinishedSemaphores[i]));
        }
    }

    void VKDevice::DestroySyncObjects()
    {
        for (auto& f : m_frames)
        {
            if (f.imageAvailable) vkDestroySemaphore(m_context->GetDevice(), f.imageAvailable, nullptr);
            // renderFinished 已移到 m_imageRenderFinishedSemaphores，此处不再持有
            if (f.inFlightFence)  vkDestroyFence    (m_context->GetDevice(), f.inFlightFence,  nullptr);
        }
        m_frames.clear();
        for (auto& s : m_imageRenderFinishedSemaphores)
            if (s) vkDestroySemaphore(m_context->GetDevice(), s, nullptr);
        m_imageRenderFinishedSemaphores.clear();
    }

    // ------------------------------------------------------------------------
    // 内存辅助
    // ------------------------------------------------------------------------
    void VKDevice::AllocateBufferMemory(VKBufferEntry& entry, VkMemoryPropertyFlags props)
    {
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(m_context->GetDevice(), entry.buffer, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = m_context->FindMemoryType(req.memoryTypeBits, props);

#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追：当 buffer 声明 SHADER_DEVICE_ADDRESS 用途时，
        // 分配内存必须挂接 VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT，否则后续
        // vkGetBufferDeviceAddress 非法。默认（无 device address）路径行为不变。
        VkMemoryAllocateFlagsInfo flagsInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
        if (entry.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        {
            flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            ai.pNext = &flagsInfo;
        }
#endif

        VK_CHECK(vkAllocateMemory(m_context->GetDevice(), &ai, nullptr, &entry.memory));
        VK_CHECK(vkBindBufferMemory(m_context->GetDevice(), entry.buffer, entry.memory, 0));

        // Host-visible 内存做持久映射，便于 UpdateBuffer 直接 memcpy
        if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            VK_CHECK(vkMapMemory(m_context->GetDevice(), entry.memory, 0, req.size, 0, &entry.mappedPtr));
        }
    }

#if defined(RENDERER_ENABLE_RAY_TRACING)
    // ------------------------------------------------------------------------
    // 光追：加速结构辅助与创建/销毁实现
    // ------------------------------------------------------------------------
    bool VKDevice::CreateRawBuffer(VkDeviceSize size,
                                   VkBufferUsageFlags usage,
                                   VkMemoryPropertyFlags props,
                                   VkBuffer& outBuffer,
                                   VkDeviceMemory& outMemory)
    {
        VkBufferCreateInfo ci{};
        ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size        = size;
        ci.usage       = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_context->GetDevice(), &ci, nullptr, &outBuffer) != VK_SUCCESS)
            return false;

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(m_context->GetDevice(), outBuffer, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = m_context->FindMemoryType(req.memoryTypeBits, props);

        VkMemoryAllocateFlagsInfo flagsInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        {
            flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            ai.pNext = &flagsInfo;
        }

        if (vkAllocateMemory(m_context->GetDevice(), &ai, nullptr, &outMemory) != VK_SUCCESS)
        {
            vkDestroyBuffer(m_context->GetDevice(), outBuffer, nullptr);
            outBuffer = VK_NULL_HANDLE;
            return false;
        }
        VK_CHECK(vkBindBufferMemory(m_context->GetDevice(), outBuffer, outMemory, 0));
        return true;
    }

    VkDeviceAddress VKDevice::GetBufferDeviceAddress(VkBuffer buffer) const
    {
        VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        info.buffer = buffer;
        return m_context->RT().vkGetBufferDeviceAddressKHR(m_context->GetDevice(), &info);
    }

    const VKAccelStructEntry* VKDevice::LookupAccelStruct(AccelerationStructureHandle h) const
    {
        auto it = m_accelStructs.find(h.id);
        return it == m_accelStructs.end() ? nullptr : &it->second;
    }

    namespace
    {
        // 后端无关 ASBuildFlags → VkBuildAccelerationStructureFlagsKHR
        VkBuildAccelerationStructureFlagsKHR ToVkASBuildFlags(ASBuildFlags f)
        {
            VkBuildAccelerationStructureFlagsKHR out = 0;
            if (HasFlag(f, ASBuildFlags::PreferFastTrace)) out |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            if (HasFlag(f, ASBuildFlags::PreferFastBuild)) out |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
            if (HasFlag(f, ASBuildFlags::AllowUpdate))     out |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
            if (HasFlag(f, ASBuildFlags::AllowCompaction)) out |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
            return out;
        }
    }

    bool VKDevice::CreateAccelerationStructureImpl(uint64_t id, const AccelerationStructureDesc& desc)
    {
        if (!m_context->SupportsRayTracing())
        {
            LOG_STREAM_ERROR("VKDevice") << "CreateAccelerationStructureImpl: device does not support ray tracing";
            return false;
        }
        const auto& rt = m_context->RT();
        const bool isBlas = desc.type == AccelerationStructureType::BottomLevel;

        // ---- 1) 组装几何 + 每几何 primitive 数 ----
        std::vector<VkAccelerationStructureGeometryKHR> geoms;
        std::vector<uint32_t>                           primCounts;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;

        VKAccelStructEntry entry{};
        entry.type = desc.type;

        if (isBlas)
        {
            geoms.reserve(desc.geometries.size());
            primCounts.reserve(desc.geometries.size());
            ranges.reserve(desc.geometries.size());
            for (const auto& g : desc.geometries)
            {
                const VKBufferEntry* vb = LookupBuffer(g.vertexBuffer);
                if (!vb)
                {
                    LOG_STREAM_ERROR("VKDevice") << "BLAS geometry: invalid vertex buffer";
                    return false;
                }
                const VKBufferEntry* ib = g.indexBuffer.IsValid() ? LookupBuffer(g.indexBuffer) : nullptr;

                VkAccelerationStructureGeometryKHR geo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
                geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                geo.flags        = g.opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;

                auto& tri = geo.geometry.triangles;
                tri.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                tri.vertexFormat  = ToVkFormat(g.vertexFormat);
                tri.vertexData.deviceAddress = GetBufferDeviceAddress(vb->buffer) + g.vertexOffset;
                tri.vertexStride  = g.vertexStride;
                tri.maxVertex     = g.vertexCount > 0 ? (g.vertexCount - 1) : 0;
                tri.indexType     = ib ? ToVkIndexType(g.indexType) : VK_INDEX_TYPE_NONE_KHR;
                tri.indexData.deviceAddress = ib ? (GetBufferDeviceAddress(ib->buffer) + g.indexOffset) : 0;
                tri.transformData.deviceAddress = 0;

                const uint32_t triCount = ib ? (g.indexCount / 3) : (g.vertexCount / 3);
                geoms.push_back(geo);
                primCounts.push_back(triCount);

                VkAccelerationStructureBuildRangeInfoKHR range{};
                range.primitiveCount  = triCount;
                range.primitiveOffset = 0;
                range.firstVertex     = 0;
                range.transformOffset = 0;
                ranges.push_back(range);
            }
        }
        else
        {
            // ---- TLAS：打包 instance 数组并上传到 host-visible instance buffer ----
            std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
            vkInstances.reserve(desc.instances.size());
            for (const auto& inst : desc.instances)
            {
                const VKAccelStructEntry* blas = LookupAccelStruct(inst.blas);
                if (!blas)
                {
                    LOG_STREAM_ERROR("VKDevice") << "TLAS instance: invalid BLAS handle";
                    return false;
                }
                VkAccelerationStructureInstanceKHR vi{};
                std::memcpy(&vi.transform, inst.transform, sizeof(float) * 12); // 4x3 行主序
                vi.instanceCustomIndex                    = inst.instanceCustomIndex & 0xFFFFFF;
                vi.mask                                   = inst.mask & 0xFF;
                vi.instanceShaderBindingTableRecordOffset = inst.shaderBindingTableOffset & 0xFFFFFF;
                vi.flags                                  = inst.flags & 0xFF;
                vi.accelerationStructureReference         = blas->deviceAddress;
                vkInstances.push_back(vi);
            }

            const VkDeviceSize instBytes =
                sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size();
            if (!CreateRawBuffer(instBytes,
                                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 entry.instanceBuffer, entry.instanceMemory))
            {
                LOG_STREAM_ERROR("VKDevice") << "TLAS: create instance buffer failed";
                return false;
            }
            void* mapped = nullptr;
            VK_CHECK(vkMapMemory(m_context->GetDevice(), entry.instanceMemory, 0, instBytes, 0, &mapped));
            std::memcpy(mapped, vkInstances.data(), static_cast<size_t>(instBytes));
            vkUnmapMemory(m_context->GetDevice(), entry.instanceMemory);

            VkAccelerationStructureGeometryKHR geo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            geo.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            geo.geometry.instances.arrayOfPointers = VK_FALSE;
            entry.instanceBufferAddr = GetBufferDeviceAddress(entry.instanceBuffer);
            geo.geometry.instances.data.deviceAddress = entry.instanceBufferAddr;
            geoms.push_back(geo);

            const uint32_t instCount = static_cast<uint32_t>(vkInstances.size());
            primCounts.push_back(instCount);
            VkAccelerationStructureBuildRangeInfoKHR range{};
            range.primitiveCount = instCount;
            ranges.push_back(range);

            entry.instanceCapacity = instCount; // refit 时不得超过此容量
        }

        // 动态更新：记录 build flags / AllowUpdate，供命令流内 refit 使用。
        entry.buildFlags  = ToVkASBuildFlags(desc.buildFlags);
        entry.allowUpdate = HasFlag(desc.buildFlags, ASBuildFlags::AllowUpdate);

        // ---- 2) 求构建尺寸 ----
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildInfo.type  = isBlas ? VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
                                 : VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = entry.buildFlags;
        buildInfo.mode  = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = static_cast<uint32_t>(geoms.size());
        buildInfo.pGeometries   = geoms.data();

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        rt.vkGetAccelerationStructureBuildSizesKHR(
            m_context->GetDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo, primCounts.data(), &sizeInfo);

        // ---- 3) 分配 backing buffer + 创建 AS ----
        if (!CreateRawBuffer(sizeInfo.accelerationStructureSize,
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             entry.buffer, entry.memory))
        {
            LOG_STREAM_ERROR("VKDevice") << "AS: create backing buffer failed";
            if (entry.instanceBuffer) vkDestroyBuffer(m_context->GetDevice(), entry.instanceBuffer, nullptr);
            if (entry.instanceMemory) vkFreeMemory(m_context->GetDevice(), entry.instanceMemory, nullptr);
            return false;
        }

        VkAccelerationStructureCreateInfoKHR asci{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        asci.buffer = entry.buffer;
        asci.size   = sizeInfo.accelerationStructureSize;
        asci.type   = buildInfo.type;
        if (rt.vkCreateAccelerationStructureKHR(m_context->GetDevice(), &asci, nullptr, &entry.as) != VK_SUCCESS)
        {
            LOG_STREAM_ERROR("VKDevice") << "vkCreateAccelerationStructureKHR failed";
            vkDestroyBuffer(m_context->GetDevice(), entry.buffer, nullptr);
            vkFreeMemory(m_context->GetDevice(), entry.memory, nullptr);
            if (entry.instanceBuffer) vkDestroyBuffer(m_context->GetDevice(), entry.instanceBuffer, nullptr);
            if (entry.instanceMemory) vkFreeMemory(m_context->GetDevice(), entry.instanceMemory, nullptr);
            return false;
        }

        // ---- 4) 分配 scratch buffer（含对齐余量）----
        //   AllowUpdate 的 TLAS：scratch 需覆盖 build 与 update 两种尺寸，且保留在
        //   entry 中供后续命令流 refit 复用（不在本函数末尾释放）。
        const uint32_t scratchAlign =
            m_context->GetAccelStructProps().minAccelerationStructureScratchOffsetAlignment;
        VkDeviceSize scratchSize = sizeInfo.buildScratchSize;
        if (entry.allowUpdate)
            scratchSize = std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize);

        VkBuffer       scratchBuffer = VK_NULL_HANDLE;
        VkDeviceMemory scratchMemory = VK_NULL_HANDLE;
        if (!CreateRawBuffer(scratchSize + scratchAlign,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             scratchBuffer, scratchMemory))
        {
            LOG_STREAM_ERROR("VKDevice") << "AS: create scratch buffer failed";
            rt.vkDestroyAccelerationStructureKHR(m_context->GetDevice(), entry.as, nullptr);
            vkDestroyBuffer(m_context->GetDevice(), entry.buffer, nullptr);
            vkFreeMemory(m_context->GetDevice(), entry.memory, nullptr);
            if (entry.instanceBuffer) vkDestroyBuffer(m_context->GetDevice(), entry.instanceBuffer, nullptr);
            if (entry.instanceMemory) vkFreeMemory(m_context->GetDevice(), entry.instanceMemory, nullptr);
            return false;
        }
        VkDeviceAddress scratchAddr = GetBufferDeviceAddress(scratchBuffer);
        if (scratchAlign > 1)
        {
            const VkDeviceAddress mask = static_cast<VkDeviceAddress>(scratchAlign) - 1;
            scratchAddr = (scratchAddr + mask) & ~mask;
        }

        // ---- 5) one-shot 命令执行构建 ----
        buildInfo.dstAccelerationStructure  = entry.as;
        buildInfo.scratchData.deviceAddress = scratchAddr;

        const VkAccelerationStructureBuildRangeInfoKHR* pRange = ranges.data();
        VkCommandBuffer cb = BeginOneTimeCommands();
        rt.vkCmdBuildAccelerationStructuresKHR(cb, 1, &buildInfo, &pRange);
        EndOneTimeCommands(cb);

        // ---- 6) 记录 device address ----
        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
        addrInfo.accelerationStructure = entry.as;
        entry.deviceAddress = rt.vkGetAccelerationStructureDeviceAddressKHR(m_context->GetDevice(), &addrInfo);

        // ---- 7) scratch：AllowUpdate 时保留供 refit，否则释放 ----
        if (entry.allowUpdate)
        {
            entry.updateScratch     = scratchBuffer;
            entry.updateScratchMem  = scratchMemory;
            entry.updateScratchAddr = scratchAddr;
        }
        else
        {
            vkDestroyBuffer(m_context->GetDevice(), scratchBuffer, nullptr);
            vkFreeMemory(m_context->GetDevice(), scratchMemory, nullptr);
        }

        m_accelStructs.emplace(id, entry);
        return true;
    }

    void VKDevice::DeleteAccelerationStructureImpl(uint64_t id)
    {
        auto it = m_accelStructs.find(id);
        if (it == m_accelStructs.end()) return;
        auto& e = it->second;
        if (e.as)              m_context->RT().vkDestroyAccelerationStructureKHR(m_context->GetDevice(), e.as, nullptr);
        if (e.buffer)          vkDestroyBuffer(m_context->GetDevice(), e.buffer, nullptr);
        if (e.memory)          vkFreeMemory(m_context->GetDevice(), e.memory, nullptr);
        if (e.instanceBuffer)  vkDestroyBuffer(m_context->GetDevice(), e.instanceBuffer, nullptr);
        if (e.instanceMemory)  vkFreeMemory(m_context->GetDevice(), e.instanceMemory, nullptr);
        if (e.updateScratch)   vkDestroyBuffer(m_context->GetDevice(), e.updateScratch, nullptr);
        if (e.updateScratchMem)vkFreeMemory(m_context->GetDevice(), e.updateScratchMem, nullptr);
        m_accelStructs.erase(it);
    }
#endif // RENDERER_ENABLE_RAY_TRACING

    // ------------------------------------------------------------------------
    // VK 后端自管 VkWindow，外部主循环通过 IsWindowClosed 问询关闭状态。
    // ------------------------------------------------------------------------
    bool VKDevice::IsWindowClosed() const
    {
        if (!m_windowPtr) return false;
        GLFWwindow* w = m_windowPtr->GetWindow();
        return (w != nullptr) && (glfwWindowShouldClose(w) != 0);
    }

    // ------------------------------------------------------------------------
    // 暴露 GLFWwindow* 给 RendererInterface::INPUT_MANAGER 查询输入。
    // ------------------------------------------------------------------------
    void* VKDevice::GetWindowNativeHandle() const
    {
        if (!m_windowPtr) return nullptr;
        return static_cast<void*>(m_windowPtr->GetWindow());
    }

    void VKDevice::FillCaps()
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_context->GetPhysicalDevice(), &props);

        m_caps.maxTextureSize2D     = props.limits.maxImageDimension2D;
        m_caps.maxTextureSize3D     = props.limits.maxImageDimension3D;
        m_caps.maxTextureSizeCube   = props.limits.maxImageDimensionCube;
        m_caps.maxColorAttachments  = props.limits.maxColorAttachments;
        {
            // 颜色与深度附件都要 MSAA 时，取两者 sample count 位掩码的交集。
            const VkSampleCountFlags common =
                props.limits.framebufferColorSampleCounts
                & props.limits.framebufferDepthSampleCounts;
            uint32_t maxSamples = 1;
            if (common & VK_SAMPLE_COUNT_64_BIT)      maxSamples = 64;
            else if (common & VK_SAMPLE_COUNT_32_BIT) maxSamples = 32;
            else if (common & VK_SAMPLE_COUNT_16_BIT) maxSamples = 16;
            else if (common & VK_SAMPLE_COUNT_8_BIT)  maxSamples = 8;
            else if (common & VK_SAMPLE_COUNT_4_BIT)  maxSamples = 4;
            else if (common & VK_SAMPLE_COUNT_2_BIT)  maxSamples = 2;
            m_caps.maxColorSampleCount = maxSamples;
        }
        m_caps.maxVertexAttributes  = props.limits.maxVertexInputAttributes;
        m_caps.maxBoundDescriptorSets = props.limits.maxBoundDescriptorSets;

        VkPhysicalDeviceFeatures feats{};
        vkGetPhysicalDeviceFeatures(m_context->GetPhysicalDevice(), &feats);
        m_caps.supportsAnisotropy        = feats.samplerAnisotropy == VK_TRUE;
        m_caps.supportsGeometryShader    = feats.geometryShader    == VK_TRUE;
        m_caps.supportsTessellation      = feats.tessellationShader == VK_TRUE;
        m_caps.supportsMultiDrawIndirect = feats.multiDrawIndirect == VK_TRUE;

        // 光追能力：源自 VkContext 探测结果。
        // 未定义 RENDERER_ENABLE_RAY_TRACING 时 VkContext 的 getter 恒返回 false。
        m_caps.supportsRayTracing = m_context->SupportsRayTracing();
        m_caps.supportsRayQuery   = m_context->SupportsRayQuery();
        m_caps.supportsRayTracingPipeline = m_context->SupportsRayTracingPipeline();

        m_caps.deviceName = props.deviceName;
    }

    // ------------------------------------------------------------------------
    // 资源 *Impl()：基类已分配 id；子类只关注 VkXxx 创建与映射
    // ------------------------------------------------------------------------
    bool VKDevice::CreateBufferImpl(uint64_t id, const BufferDesc& desc)
    {
        VKBufferEntry e{};
        e.size  = desc.size;
        e.usage = ToVkBufferUsage(desc.usage);
        e.memProps = ToVkMemoryProps(desc.memory);

        // GpuOnly + initialData 时需要从 staging buffer 拷贝过来，
        // 因此目标 buffer 的 usage 必须包含 TRANSFER_DST_BIT。业务侧 BufferDesc
        // 通常只声明 VertexBuffer/IndexBuffer/UniformBuffer 等"用途位"，不会
        // 主动加 TransferDst；这里在 VkBufferUsageFlags 上自动补一份。
        const bool needStagingUpload =
            desc.initialData && desc.size > 0 && desc.memory == MemoryUsage::GpuOnly;
        if (needStagingUpload)
        {
            e.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }

        VkBufferCreateInfo ci{};
        ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size        = desc.size;
        ci.usage       = e.usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(m_context->GetDevice(), &ci, nullptr, &e.buffer));

        AllocateBufferMemory(e, e.memProps);

        // 初始数据上传：
        //   - Host-visible（mappedPtr 非空）：直接 memcpy；
        //   - GpuOnly（device-local，无 mapping）：走 staging buffer 路径。
        if (desc.initialData && desc.size > 0)
        {
            if (e.mappedPtr)
            {
                std::memcpy(e.mappedPtr, desc.initialData, static_cast<size_t>(desc.size));
            }
            else
            {
                UploadBufferViaStaging(e.buffer, 0, desc.initialData, desc.size);
            }
        }

        m_buffers.emplace(id, e);
        return true;
    }

    void VKDevice::DeleteBufferImpl(uint64_t id)
    {
        auto it = m_buffers.find(id);
        if (it == m_buffers.end()) return;
        if (it->second.mappedPtr && it->second.memory)
            vkUnmapMemory(m_context->GetDevice(), it->second.memory);
        if (it->second.buffer) vkDestroyBuffer(m_context->GetDevice(), it->second.buffer, nullptr);
        if (it->second.memory) vkFreeMemory   (m_context->GetDevice(), it->second.memory, nullptr);
        m_buffers.erase(it);
    }

    void VKDevice::UpdateBufferImpl(BufferHandle h, const void* src, size_t bytes, size_t dstOffset)
    {
        auto it = m_buffers.find(h.id);
        if (it == m_buffers.end()) return;
        if (!it->second.mappedPtr)
        {
                LOG_STREAM_ERROR("VKDevice") << "UpdateBuffer on non host-visible buffer not implemented.";
            return;
        }
        std::memcpy(static_cast<char*>(it->second.mappedPtr) + dstOffset, src, bytes);
    }

    // ------------------------------------------------------------------------
    // Texture（最小路径，仅创建 VkImage + Memory + Default View）
    // ------------------------------------------------------------------------
    bool VKDevice::CreateTextureImpl(uint64_t id, const TextureDesc& desc)
    {
        VKTextureEntry e{};
        e.format       = ToVkFormat(desc.format);
        e.width        = desc.width;
        e.height       = desc.height;

        // 与 GLDevice 行为一致，对 desc.mipLevels=0 / desc.arrayLayers=0 自动归一化。
        //   - mipLevels=0 表示"由后端按 max 计算"（AssetGpuUploader 的 generateMipmaps 路径会传 0）；
        //   - arrayLayers=0 一律归一化到 1（避免 vkCreateImage VUID 报错）。
        uint32_t resolvedMipLevels = desc.mipLevels;
        if (resolvedMipLevels == 0)
        {
            const uint32_t maxDim = std::max(desc.width, desc.height);
            uint32_t levels = 1;
            for (uint32_t v = maxDim; v > 1; v >>= 1) ++levels;
            resolvedMipLevels = levels;
        }
        const uint32_t resolvedArrayLayers = desc.arrayLayers ? desc.arrayLayers : 1;

        e.mipLevels    = resolvedMipLevels;
        e.arrayLayers  = resolvedArrayLayers;
        e.samples      = desc.samples ? desc.samples : 1;
        e.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = (desc.type == TextureType::Tex3D) ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        ci.format        = e.format;
        ci.extent        = { desc.width, desc.height, desc.depth ? desc.depth : 1 };
        ci.mipLevels     = resolvedMipLevels;
        ci.arrayLayers   = resolvedArrayLayers;
        ci.samples       = static_cast<VkSampleCountFlagBits>(e.samples);
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = ToVkImageUsage(desc.usage);
        // MSAA 颜色必须能被 vkCmdResolveImage 读；业务侧漏标 TransferSrc 时补上。
        if (e.samples > 1 && HasFlag(desc.usage, TextureUsage::ColorAttachment))
            ci.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (desc.type == TextureType::TexCube)
            ci.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        VK_CHECK(vkCreateImage(m_context->GetDevice(), &ci, nullptr, &e.image));

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(m_context->GetDevice(), e.image, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = m_context->FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(m_context->GetDevice(), &ai, nullptr, &e.memory));
        VK_CHECK(vkBindImageMemory(m_context->GetDevice(), e.image, e.memory, 0));

        // 默认 View（color/depth 自动选择 aspect）
        VkImageViewCreateInfo vi{};
        vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image    = e.image;
        vi.viewType = (desc.type == TextureType::TexCube)    ? VK_IMAGE_VIEW_TYPE_CUBE
                    : (desc.type == TextureType::Tex3D)      ? VK_IMAGE_VIEW_TYPE_3D
                    : (desc.type == TextureType::Tex2DArray) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                             : VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = e.format;
        const bool isDepth = HasFlag(desc.usage, TextureUsage::DepthStencilAttachment);
        vi.subresourceRange.aspectMask     = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel   = 0;
        vi.subresourceRange.levelCount     = resolvedMipLevels;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount     = resolvedArrayLayers;
        VK_CHECK(vkCreateImageView(m_context->GetDevice(), &vi, nullptr, &e.defaultView));

        // Storage Image 必须从 UNDEFINED 转到 GENERAL，否则 Compute Shader
        // 中 imageStore 会触发 Validation Layer "Image Layout invalid" 报错。
        // 这里走 immediate one-shot CmdBuffer 把 layout 提前固化为 GENERAL，
        // 之后业务侧不需要再做 layout 转换（GENERAL 同时支持 storage read/write 和 sampled）。
        if (HasFlag(desc.usage, TextureUsage::Storage))
        {
            TransitionImageLayoutImmediate(e.image,
                                           vi.subresourceRange.aspectMask,
                                           resolvedMipLevels, resolvedArrayLayers,
                                           VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_GENERAL);
            e.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        m_textures.emplace(id, e);
        return true;
    }

    void VKDevice::DeleteTextureImpl(uint64_t id)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end()) return;
        if (it->second.defaultView) vkDestroyImageView(m_context->GetDevice(), it->second.defaultView, nullptr);
        if (it->second.image)       vkDestroyImage    (m_context->GetDevice(), it->second.image, nullptr);
        if (it->second.memory)      vkFreeMemory      (m_context->GetDevice(), it->second.memory, nullptr);
        m_textures.erase(it);
    }

    void VKDevice::UpdateTextureImpl(TextureHandle texture, const TextureUploadDesc& upload)
    {
        // 通过 staging buffer 上传像素数据。
        //   1) 找到目标 VKTextureEntry；
        //   2) 调用 UploadImageViaStaging（内部完成 layout UNDEFINED→TRANSFER_DST→finalLayout
        //      和 vkCmdCopyBufferToImage）；
        //   3) 更新 entry.currentLayout。
        // 注意：当 image 已经是 Storage Image（layout=GENERAL）时不能再转回 SHADER_READ_ONLY_OPTIMAL，
        // 否则 Compute Shader 的后续 imageStore 会触发 "Layout mismatch"。这里按 currentLayout
        // 是否 GENERAL 决定 finalLayout。
        if (!upload.data || upload.bytes == 0) return;

        auto it = m_textures.find(texture.id);
        if (it == m_textures.end()) return;

        VKTextureEntry& e = it->second;
        if (e.image == VK_NULL_HANDLE) return;

        // 推断 aspect：由 entry 创建时的 view aspect 决定。这里简化按 color 处理
        // （depth/stencil 纹理本身不会走 UpdateTexture 路径，业务通常只更新 color 纹理）。
        const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        const VkImageLayout finalLayout =
            (e.currentLayout == VK_IMAGE_LAYOUT_GENERAL)
                ? VK_IMAGE_LAYOUT_GENERAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        UploadImageViaStaging(e.image,
                              aspect,
                              upload.mipLevel,
                              upload.arrayLayer,
                              e.mipLevels,
                              e.arrayLayers,
                              upload.width  ? upload.width  : e.width,
                              upload.height ? upload.height : e.height,
                              upload.depth  ? upload.depth  : 1,
                              upload.data,
                              upload.bytes,
                              finalLayout);

        if (e.currentLayout != VK_IMAGE_LAYOUT_GENERAL)
        {
            e.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    // ------------------------------------------------------------------------
    // Sampler
    // ------------------------------------------------------------------------
    bool VKDevice::CreateSamplerImpl(uint64_t id, const SamplerDesc& desc)
    {
        VKSamplerEntry e{};
        VkSamplerCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.minFilter    = ToVkFilter(desc.minFilter);
        ci.magFilter    = ToVkFilter(desc.magFilter);
        ci.mipmapMode   = ToVkMipmapMode(desc.mipmapMode);
        ci.addressModeU = ToVkAddressMode(desc.addressU);
        ci.addressModeV = ToVkAddressMode(desc.addressV);
        ci.addressModeW = ToVkAddressMode(desc.addressW);
        ci.minLod       = desc.minLod;
        ci.maxLod       = desc.maxLod;
        ci.mipLodBias   = desc.mipLodBias;
        ci.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
        ci.maxAnisotropy    = desc.maxAnisotropy;
        ci.compareEnable    = desc.compareEnable ? VK_TRUE : VK_FALSE;
        ci.compareOp        = ToVkCompareOp(desc.compareOp);
        ci.borderColor      = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        VK_CHECK(vkCreateSampler(m_context->GetDevice(), &ci, nullptr, &e.sampler));

        m_samplers.emplace(id, e);
        return true;
    }

    void VKDevice::DeleteSamplerImpl(uint64_t id)
    {
        auto it = m_samplers.find(id);
        if (it == m_samplers.end()) return;
        if (it->second.sampler) vkDestroySampler(m_context->GetDevice(), it->second.sampler, nullptr);
        m_samplers.erase(it);
    }

    // ------------------------------------------------------------------------
    // Shader（接收 SPIR-V 字节码或 GLSL 源码）
    //   推荐 GLSL 文本路径——走 glslang 在线编译为 SPIR-V，让
    //   GL/VK 两端可以共用同一份 *.glsl，避免预编译 .spv 产物与源码不同步。
    //   只要 desc.code 首 4 字节是 SPIR-V magic word(0x07230203) 就直接使用，
    //   其他情况当作 GLSL 文本处理。
    // ------------------------------------------------------------------------
    bool VKDevice::CreateShaderImpl(uint64_t id, const ShaderDesc& desc)
    {
        VKShaderEntry e{};
        e.stage = ToVkShaderStage(desc.stage);
        e.entry = (desc.entryPoint && *desc.entryPoint) ? desc.entryPoint : "main";
        e.reflection = desc.reflection;

        const uint32_t* spvWords    = nullptr;
        size_t          spvByteSize = 0;
        std::vector<uint32_t> compiledSpv;  // GLSL 路径下保住编译产物

        if (GuessIsSpirv(desc.code, desc.bytes))
        {
            // 预编译 SPIR-V：直接送入 vkCreateShaderModule
            spvWords    = static_cast<const uint32_t*>(desc.code);
            spvByteSize = desc.bytes;
        }
        else
        {
            // GLSL 文本：走 glslang 在线编译
            const char* src = static_cast<const char*>(desc.code);
            const char* dbg = desc.debugName ? desc.debugName : "<glsl>";
            if (!CompileGlslToSpirv(desc.stage, src, desc.bytes, dbg, compiledSpv))
            {
                LOG_STREAM_ERROR("VKDevice") << "CreateShader: glsl->spirv compile failed: "
                          << dbg;
                return false;
            }
            spvWords    = compiledSpv.data();
            spvByteSize = compiledSpv.size() * sizeof(uint32_t);
        }

        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spvByteSize;
        ci.pCode    = spvWords;
        VK_CHECK(vkCreateShaderModule(m_context->GetDevice(), &ci, nullptr, &e.module));

        m_shaders.emplace(id, e);
        return true;
    }

    void VKDevice::DeleteShaderImpl(uint64_t id)
    {
        auto it = m_shaders.find(id);
        if (it == m_shaders.end()) return;
        if (it->second.module) vkDestroyShaderModule(m_context->GetDevice(), it->second.module, nullptr);
        m_shaders.erase(it);
    }

    // ------------------------------------------------------------------------
    // Pipeline
    // ------------------------------------------------------------------------
    bool VKDevice::CreatePipelineImpl(uint64_t id, const GraphicsPipelineDesc& desc)
    {
        VKPipelineEntry pe{};

        // 1) 构建 DescriptorSetLayouts（按 set 索引分组 bindings）
        std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> setBindings;
        for (const auto& rb : desc.resourceBindings)
        {
            VkDescriptorSetLayoutBinding b{};
            b.binding         = rb.binding;
            b.descriptorType  = ToVkDescriptorType(rb.type);
            b.descriptorCount = rb.count;
            b.stageFlags      = ToVkShaderStageFlags(rb.stages);
            setBindings[rb.set].push_back(b);
        }
        uint32_t maxSet = 0;
        for (auto& kv : setBindings) maxSet = std::max(maxSet, kv.first);
        if (!setBindings.empty()) pe.setLayouts.resize(maxSet + 1, VK_NULL_HANDLE);
        for (auto& kv : setBindings)
        {
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = static_cast<uint32_t>(kv.second.size());
            ci.pBindings    = kv.second.data();
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            VK_CHECK(vkCreateDescriptorSetLayout(m_context->GetDevice(), &ci, nullptr, &layout));
            pe.setLayouts[kv.first] = layout;
        }

        // 2) 选取目标 RenderPass：
        //    根据 desc.rtLayout 构建一个仅用于 pipeline compatibility 的
        //    临时 VkRenderPass（VK 要求 pipeline.renderPass 与实际 BeginRenderPass
        //    使用的 RenderPass "兼容"——attachment 数量、format、samples 一致）。
        //    若业务未指定 colorFormats（空），退化到 swapchain 默认 RP（单色 attachment）。
        VkRenderPass rp = VK_NULL_HANDLE;
        bool ownsRp = false;
        if (desc.rtLayout.colorFormats.empty()
            && desc.rtLayout.depthStencilFormat == Format::Undefined)
        {
            rp = m_swapchain ? m_swapchain->GetDefaultRenderPass() : VK_NULL_HANDLE;
            ownsRp = false;
        }
        else
        {
            rp = CreateCompatibleRenderPass(desc.rtLayout);
            ownsRp = (rp != VK_NULL_HANDLE);
            if (rp == VK_NULL_HANDLE)
            {
                // 兜底：构建失败时回退到 swapchain 默认 RP，保证不崩
                rp = m_swapchain ? m_swapchain->GetDefaultRenderPass() : VK_NULL_HANDLE;
                ownsRp = false;
            }
        }
        pe.compatRenderPass     = rp;
        pe.ownsCompatRenderPass = ownsRp;

        // 3) 顶点输入
        std::vector<VkVertexInputBindingDescription>   vBindings;
        std::vector<VkVertexInputAttributeDescription> vAttribs;
        for (const auto& vb : desc.vertexLayout.bindings)
        {
            VkVertexInputBindingDescription d{};
            d.binding   = vb.binding;
            d.stride    = vb.stride;
            d.inputRate = (vb.inputRate == VertexInputRate::Vertex)
                          ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
            vBindings.push_back(d);
        }
        for (const auto& va : desc.vertexLayout.attributes)
        {
            VkVertexInputAttributeDescription d{};
            d.location = va.location;
            d.binding  = va.binding;
            d.format   = ToVkFormat(va.format);
            d.offset   = va.offset;
            vAttribs.push_back(d);
        }

        // 4) 着色器阶段
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        auto addStage = [&](ShaderHandle h)
        {
            if (!h.IsValid()) return;
            const VKShaderEntry* se = LookupShader(h);
            if (!se) return;
            VkPipelineShaderStageCreateInfo s{};
            s.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            s.stage  = se->stage;
            s.module = se->module;
            s.pName  = se->entry.c_str();
            stages.push_back(s);
        };
        addStage(desc.vertexShader);
        addStage(desc.fragmentShader);
        addStage(desc.geometryShader);

        // 5) Input Assembly / Rasterizer / DepthStencil / Blend / Multisample / Dynamic
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = static_cast<uint32_t>(vBindings.size());
        vi.pVertexBindingDescriptions      = vBindings.data();
        vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(vAttribs.size());
        vi.pVertexAttributeDescriptions    = vAttribs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = ToVkTopology(desc.topology);

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode      = ToVkPolygonMode(desc.rasterizer.polygonMode);
        rs.cullMode         = ToVkCullMode(desc.rasterizer.cullMode);
        rs.frontFace        = ToVkFrontFace(desc.rasterizer.frontFace);
        rs.lineWidth        = desc.rasterizer.lineWidth;
        rs.depthClampEnable = desc.rasterizer.depthClampEnable ? VK_TRUE : VK_FALSE;
        rs.rasterizerDiscardEnable = desc.rasterizer.rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = static_cast<VkSampleCountFlagBits>(
            desc.rtLayout.samples ? desc.rtLayout.samples : 1);

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = ToVkCompareOp(desc.depthStencil.depthCompareOp);
        ds.stencilTestEnable = desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;

        std::vector<VkPipelineColorBlendAttachmentState> blendAtts;
        if (desc.blend.attachments.empty())
        {
            VkPipelineColorBlendAttachmentState d{};
            d.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            d.blendEnable    = VK_FALSE;
            blendAtts.push_back(d);
        }
        else
        {
            for (const auto& a : desc.blend.attachments)
            {
                VkPipelineColorBlendAttachmentState d{};
                d.blendEnable         = a.blendEnable ? VK_TRUE : VK_FALSE;
                d.srcColorBlendFactor = ToVkBlendFactor(a.srcColorBlendFactor);
                d.dstColorBlendFactor = ToVkBlendFactor(a.dstColorBlendFactor);
                d.colorBlendOp        = ToVkBlendOp(a.colorBlendOp);
                d.srcAlphaBlendFactor = ToVkBlendFactor(a.srcAlphaBlendFactor);
                d.dstAlphaBlendFactor = ToVkBlendFactor(a.dstAlphaBlendFactor);
                d.alphaBlendOp        = ToVkBlendOp(a.alphaBlendOp);
                d.colorWriteMask      = a.colorWriteMask;
                blendAtts.push_back(d);
            }
        }
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = static_cast<uint32_t>(blendAtts.size());
        cb.pAttachments    = blendAtts.data();

        VkDynamicState dyns[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dy{};
        dy.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dy.dynamicStateCount = 2;
        dy.pDynamicStates    = dyns;

        // 6) PipelineLayout
        // 按 stageFlags 分组合并 PushConstantRange。
        // 业务侧（按 GL 习惯）会为每个 uniform 注册一个独立 PushConstantRange，
        // 但 Vulkan VUID-VkPipelineLayoutCreateInfo-pPushConstantRanges-00292
        // 要求同一 stage 在 PipelineLayout 中只能出现一个 PushConstantRange。
        // 这里把同 stageFlags 的所有 ranges 合并为单个 [minOffset, maxEnd) 范围。
        std::vector<VkPushConstantRange> pcs;
        {
            std::unordered_map<VkShaderStageFlags, VkPushConstantRange> byStage;
            for (const auto& p : desc.pushConstantRanges)
            {
                if (p.size == 0) continue;
                const VkShaderStageFlags stage = ToVkShaderStageFlags(p.stages);
                auto it = byStage.find(stage);
                if (it == byStage.end())
                {
                    VkPushConstantRange r{};
                    r.stageFlags = stage;
                    r.offset     = p.offset;
                    r.size       = p.size;
                    byStage[stage] = r;
                }
                else
                {
                    const uint32_t newEnd = std::max(it->second.offset + it->second.size,
                                                     p.offset + p.size);
                    it->second.offset = std::min(it->second.offset, p.offset);
                    it->second.size   = newEnd - it->second.offset;
                }
            }
            pcs.reserve(byStage.size());
            for (auto& kv : byStage) pcs.push_back(kv.second);
        }
        VkPipelineLayoutCreateInfo li{};
        li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount         = static_cast<uint32_t>(pe.setLayouts.size());
        li.pSetLayouts            = pe.setLayouts.empty() ? nullptr : pe.setLayouts.data();
        li.pushConstantRangeCount = static_cast<uint32_t>(pcs.size());
        li.pPushConstantRanges    = pcs.empty() ? nullptr : pcs.data();
        VK_CHECK(vkCreatePipelineLayout(m_context->GetDevice(), &li, nullptr, &pe.layout));

        // 7) 创建 Pipeline
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount          = static_cast<uint32_t>(stages.size());
        pi.pStages             = stages.data();
        pi.pVertexInputState   = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState      = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState   = &ms;
        pi.pDepthStencilState  = &ds;
        pi.pColorBlendState    = &cb;
        pi.pDynamicState       = &dy;
        pi.layout              = pe.layout;
        pi.renderPass          = rp;
        pi.subpass             = 0;
        VK_CHECK(vkCreateGraphicsPipelines(m_context->GetDevice(), VK_NULL_HANDLE, 1, &pi, nullptr, &pe.pipeline));

        // 显式标记为图形管线，供 BindPipeline 选择 BindPoint
        pe.isCompute = false;
        pe.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

        m_pipelines.emplace(id, pe);
        return true;
    }

    // ------------------------------------------------------------------------
    // Compute Pipeline
    //   - 与 GraphicsPipeline 同句柄空间，isCompute=true 区分
    //   - 根据 resourceBindings 按 set 构造 DescriptorSetLayout（与 Graphics 同逻辑）
    //   - 由 PushConstantRange 构造 PipelineLayout
    //   - 这里仅创建 Pipeline；DescriptorPool / VkDescriptorSet 分配在 BindResourceSet 路径。
    // ------------------------------------------------------------------------
    bool VKDevice::CreatePipelineImpl(uint64_t id, const ComputePipelineDesc& desc)
    {
        if (!desc.computeShader.IsValid())
        {
            LOG_STREAM_ERROR("VKDevice") << "CreatePipeline(Compute): cs not set";
            return false;
        }
        const VKShaderEntry* cs = LookupShader(desc.computeShader);
        if (!cs || cs->stage != VK_SHADER_STAGE_COMPUTE_BIT)
        {
            LOG_STREAM_ERROR("VKDevice") << "CreatePipeline(Compute): shader handle not a compute shader";
            return false;
        }

        VKPipelineEntry pe{};
        pe.isCompute = true;
        pe.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

        // 1) DescriptorSetLayouts（与 GraphicsPipeline 一致的构造逻辑）
        std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> setBindings;
        for (const auto& rb : desc.resourceBindings)
        {
            VkDescriptorSetLayoutBinding b{};
            b.binding         = rb.binding;
            b.descriptorType  = ToVkDescriptorType(rb.type);
            b.descriptorCount = rb.count;
            b.stageFlags      = ToVkShaderStageFlags(rb.stages);
            setBindings[rb.set].push_back(b);
        }
        uint32_t maxSet = 0;
        for (auto& kv : setBindings) maxSet = std::max(maxSet, kv.first);
        if (!setBindings.empty()) pe.setLayouts.resize(maxSet + 1, VK_NULL_HANDLE);
        for (auto& kv : setBindings)
        {
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = static_cast<uint32_t>(kv.second.size());
            ci.pBindings    = kv.second.data();
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            VK_CHECK(vkCreateDescriptorSetLayout(m_context->GetDevice(), &ci, nullptr, &layout));
            pe.setLayouts[kv.first] = layout;
        }

        // 2) PipelineLayout
        // 按 stageFlags 分组合并 PushConstantRange（理由同 graphics pipeline 路径）。
        std::vector<VkPushConstantRange> pcs;
        {
            std::unordered_map<VkShaderStageFlags, VkPushConstantRange> byStage;
            for (const auto& p : desc.pushConstantRanges)
            {
                if (p.size == 0) continue;
                const VkShaderStageFlags stage = ToVkShaderStageFlags(p.stages);
                auto it = byStage.find(stage);
                if (it == byStage.end())
                {
                    VkPushConstantRange r{};
                    r.stageFlags = stage;
                    r.offset     = p.offset;
                    r.size       = p.size;
                    byStage[stage] = r;
                }
                else
                {
                    const uint32_t newEnd = std::max(it->second.offset + it->second.size,
                                                     p.offset + p.size);
                    it->second.offset = std::min(it->second.offset, p.offset);
                    it->second.size   = newEnd - it->second.offset;
                }
            }
            pcs.reserve(byStage.size());
            for (auto& kv : byStage) pcs.push_back(kv.second);
        }
        VkPipelineLayoutCreateInfo li{};
        li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount         = static_cast<uint32_t>(pe.setLayouts.size());
        li.pSetLayouts            = pe.setLayouts.empty() ? nullptr : pe.setLayouts.data();
        li.pushConstantRangeCount = static_cast<uint32_t>(pcs.size());
        li.pPushConstantRanges    = pcs.empty() ? nullptr : pcs.data();
        VK_CHECK(vkCreatePipelineLayout(m_context->GetDevice(), &li, nullptr, &pe.layout));

        // 3) Compute Shader Stage
        VkPipelineShaderStageCreateInfo ss{};
        ss.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ss.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        ss.module = cs->module;
        ss.pName  = cs->entry.c_str();

        VkComputePipelineCreateInfo ci{};
        ci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage  = ss;
        ci.layout = pe.layout;
        VK_CHECK(vkCreateComputePipelines(m_context->GetDevice(), VK_NULL_HANDLE, 1, &ci, nullptr, &pe.pipeline));

        m_pipelines.emplace(id, pe);
        return true;
    }

#if defined(RENDERER_ENABLE_RAY_TRACING)
    // ------------------------------------------------------------------------
    // 光追管线
    //   1) 构建 DescriptorSetLayout + PipelineLayout（与 compute 同款逻辑）
    //   2) stages + groups → vkCreateRayTracingPipelinesKHR
    //   3) vkGetRayTracingShaderGroupHandlesKHR 取 group handle
    //   4) 按 handle size/alignment 拷入 SBT buffer，计算 raygen/miss/hit/callable
    //      四个 region 的 device address 与 stride，记入 VKPipelineEntry
    // ------------------------------------------------------------------------
    bool VKDevice::CreatePipelineImpl(uint64_t id, const RayTracingPipelineDesc& desc)
    {
        if (!m_context->SupportsRayTracingPipeline())
        {
            LOG_STREAM_ERROR("VKDevice") << "CreatePipeline(RayTracing): device does not support RT pipeline";
            return false;
        }
        const auto& rt = m_context->RT();

        VKPipelineEntry pe{};
        pe.isRayTracing = true;
        pe.bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;

        // 1) DescriptorSetLayouts（与 compute/graphics 一致）
        std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> setBindings;
        for (const auto& rb : desc.resourceBindings)
        {
            VkDescriptorSetLayoutBinding b{};
            b.binding         = rb.binding;
            b.descriptorType  = ToVkDescriptorType(rb.type);
            b.descriptorCount = rb.count;
            b.stageFlags      = ToVkShaderStageFlags(rb.stages);
            setBindings[rb.set].push_back(b);
        }
        uint32_t maxSet = 0;
        for (auto& kv : setBindings) maxSet = std::max(maxSet, kv.first);
        if (!setBindings.empty()) pe.setLayouts.resize(maxSet + 1, VK_NULL_HANDLE);
        for (auto& kv : setBindings)
        {
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = static_cast<uint32_t>(kv.second.size());
            ci.pBindings    = kv.second.data();
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            VK_CHECK(vkCreateDescriptorSetLayout(m_context->GetDevice(), &ci, nullptr, &layout));
            pe.setLayouts[kv.first] = layout;
        }

        // 2) PipelineLayout（PushConstantRange 按 stage 合并，理由同 compute 路径）
        std::vector<VkPushConstantRange> pcs;
        {
            std::unordered_map<VkShaderStageFlags, VkPushConstantRange> byStage;
            for (const auto& p : desc.pushConstantRanges)
            {
                if (p.size == 0) continue;
                const VkShaderStageFlags stage = ToVkShaderStageFlags(p.stages);
                auto it = byStage.find(stage);
                if (it == byStage.end())
                {
                    VkPushConstantRange r{};
                    r.stageFlags = stage; r.offset = p.offset; r.size = p.size;
                    byStage[stage] = r;
                }
                else
                {
                    const uint32_t newEnd = std::max(it->second.offset + it->second.size, p.offset + p.size);
                    it->second.offset = std::min(it->second.offset, p.offset);
                    it->second.size   = newEnd - it->second.offset;
                }
            }
            pcs.reserve(byStage.size());
            for (auto& kv : byStage) pcs.push_back(kv.second);
        }
        VkPipelineLayoutCreateInfo li{};
        li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount         = static_cast<uint32_t>(pe.setLayouts.size());
        li.pSetLayouts            = pe.setLayouts.empty() ? nullptr : pe.setLayouts.data();
        li.pushConstantRangeCount = static_cast<uint32_t>(pcs.size());
        li.pPushConstantRanges    = pcs.empty() ? nullptr : pcs.data();
        VK_CHECK(vkCreatePipelineLayout(m_context->GetDevice(), &li, nullptr, &pe.layout));

        // 3) Shader stages
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.reserve(desc.stages.size());
        for (const auto& s : desc.stages)
        {
            const VKShaderEntry* sh = LookupShader(s.shader);
            if (!sh)
            {
                LOG_STREAM_ERROR("VKDevice") << "RT pipeline: invalid shader handle";
                vkDestroyPipelineLayout(m_context->GetDevice(), pe.layout, nullptr);
                for (auto& sl : pe.setLayouts) if (sl) vkDestroyDescriptorSetLayout(m_context->GetDevice(), sl, nullptr);
                return false;
            }
            VkPipelineShaderStageCreateInfo ss{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            ss.stage  = ToVkShaderStage(s.stage);
            ss.module = sh->module;
            ss.pName  = sh->entry.c_str();
            stages.push_back(ss);
        }

        // 4) Shader groups（同时按类别记录 group 索引，供 SBT 分区）
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        groups.reserve(desc.groups.size());
        std::vector<uint32_t> raygenIdx, missIdx, hitIdx, callableIdx;
        for (uint32_t gi = 0; gi < desc.groups.size(); ++gi)
        {
            const auto& g = desc.groups[gi];
            VkRayTracingShaderGroupCreateInfoKHR grp{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            grp.generalShader      = g.generalShader;
            grp.closestHitShader   = g.closestHitShader;
            grp.anyHitShader       = g.anyHitShader;
            grp.intersectionShader = g.intersectionShader;

            if (g.type == RayTracingShaderGroupType::General)
            {
                grp.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                // 用 general shader 的 stage 归类到 raygen/miss/callable
                const ShaderStage st = (g.generalShader < desc.stages.size())
                    ? desc.stages[g.generalShader].stage : ShaderStage::RayGen;
                if      (st == ShaderStage::RayGen)   raygenIdx.push_back(gi);
                else if (st == ShaderStage::Miss)     missIdx.push_back(gi);
                else if (st == ShaderStage::Callable) callableIdx.push_back(gi);
                else                                  raygenIdx.push_back(gi);
            }
            else
            {
                grp.type = (g.type == RayTracingShaderGroupType::TrianglesHit)
                    ? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR
                    : VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
                hitIdx.push_back(gi);
            }
            groups.push_back(grp);
        }

        // 5) 创建 RT 管线
        VkRayTracingPipelineCreateInfoKHR ci{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
        ci.stageCount = static_cast<uint32_t>(stages.size());
        ci.pStages    = stages.data();
        ci.groupCount = static_cast<uint32_t>(groups.size());
        ci.pGroups    = groups.data();
        ci.maxPipelineRayRecursionDepth = desc.maxRayRecursionDepth;
        ci.layout     = pe.layout;

        VkResult r = rt.vkCreateRayTracingPipelinesKHR(
            m_context->GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &ci, nullptr, &pe.pipeline);
        if (r != VK_SUCCESS)
        {
            LOG_STREAM_ERROR("VKDevice") << "vkCreateRayTracingPipelinesKHR failed (vk=" << r << ")";
            vkDestroyPipelineLayout(m_context->GetDevice(), pe.layout, nullptr);
            for (auto& sl : pe.setLayouts) if (sl) vkDestroyDescriptorSetLayout(m_context->GetDevice(), sl, nullptr);
            return false;
        }

        // 6) 取 group handles
        const auto& props = m_context->GetRTPipelineProps();
        const uint32_t handleSize = props.shaderGroupHandleSize;
        const uint32_t handleAlign = props.shaderGroupHandleAlignment;
        const uint32_t baseAlign   = props.shaderGroupBaseAlignment;
        auto alignUp = [](uint64_t v, uint64_t a) -> uint64_t { return (a == 0) ? v : ((v + a - 1) & ~(a - 1)); };
        const uint32_t handleSizeAligned = static_cast<uint32_t>(alignUp(handleSize, handleAlign));

        const uint32_t groupCount = static_cast<uint32_t>(groups.size());
        std::vector<uint8_t> handles(static_cast<size_t>(groupCount) * handleSize);
        if (rt.vkGetRayTracingShaderGroupHandlesKHR(
                m_context->GetDevice(), pe.pipeline, 0, groupCount,
                handles.size(), handles.data()) != VK_SUCCESS)
        {
            LOG_STREAM_ERROR("VKDevice") << "vkGetRayTracingShaderGroupHandlesKHR failed";
            vkDestroyPipeline(m_context->GetDevice(), pe.pipeline, nullptr);
            vkDestroyPipelineLayout(m_context->GetDevice(), pe.layout, nullptr);
            for (auto& sl : pe.setLayouts) if (sl) vkDestroyDescriptorSetLayout(m_context->GetDevice(), sl, nullptr);
            return false;
        }

        // 7) 规划 SBT 分区：raygen 区 size==stride 且按 baseAlign 对齐；
        //    miss/hit/callable 区 stride=handleSizeAligned，size 按 baseAlign 对齐。
        const uint64_t raygenStride = alignUp(handleSizeAligned, baseAlign);
        const uint64_t raygenSize   = raygenStride; // raygen 仅取第一个 raygen group
        const uint64_t missSize     = alignUp(static_cast<uint64_t>(missIdx.size())     * handleSizeAligned, baseAlign);
        const uint64_t hitSize      = alignUp(static_cast<uint64_t>(hitIdx.size())      * handleSizeAligned, baseAlign);
        const uint64_t callableSize = alignUp(static_cast<uint64_t>(callableIdx.size()) * handleSizeAligned, baseAlign);

        const uint64_t raygenOffset   = 0;
        const uint64_t missOffset     = raygenOffset + raygenSize;
        const uint64_t hitOffset      = missOffset + missSize;
        const uint64_t callableOffset = hitOffset + hitSize;
        const uint64_t sbtSize        = callableOffset + callableSize;

        if (!CreateRawBuffer(sbtSize,
                             VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             pe.sbtBuffer, pe.sbtMemory))
        {
            LOG_STREAM_ERROR("VKDevice") << "RT pipeline: create SBT buffer failed";
            vkDestroyPipeline(m_context->GetDevice(), pe.pipeline, nullptr);
            vkDestroyPipelineLayout(m_context->GetDevice(), pe.layout, nullptr);
            for (auto& sl : pe.setLayouts) if (sl) vkDestroyDescriptorSetLayout(m_context->GetDevice(), sl, nullptr);
            return false;
        }

        // 8) 拷贝 handle 到各分区
        uint8_t* mapped = nullptr;
        VK_CHECK(vkMapMemory(m_context->GetDevice(), pe.sbtMemory, 0, sbtSize, 0, reinterpret_cast<void**>(&mapped)));
        auto copyHandle = [&](uint64_t regionOffset, uint32_t slot, uint32_t groupIndex)
        {
            std::memcpy(mapped + regionOffset + static_cast<uint64_t>(slot) * handleSizeAligned,
                        handles.data() + static_cast<size_t>(groupIndex) * handleSize,
                        handleSize);
        };
        if (!raygenIdx.empty()) copyHandle(raygenOffset, 0, raygenIdx[0]);
        for (uint32_t i = 0; i < missIdx.size(); ++i)     copyHandle(missOffset, i, missIdx[i]);
        for (uint32_t i = 0; i < hitIdx.size(); ++i)      copyHandle(hitOffset, i, hitIdx[i]);
        for (uint32_t i = 0; i < callableIdx.size(); ++i) copyHandle(callableOffset, i, callableIdx[i]);
        vkUnmapMemory(m_context->GetDevice(), pe.sbtMemory);

        // 9) 计算各 region 的 device address（TraceRays 使用）
        const VkDeviceAddress sbtBase = GetBufferDeviceAddress(pe.sbtBuffer);
        pe.raygenRegion.deviceAddress   = raygenIdx.empty()   ? 0 : (sbtBase + raygenOffset);
        pe.raygenRegion.stride          = raygenStride;
        pe.raygenRegion.size            = raygenIdx.empty()   ? 0 : raygenSize;
        pe.missRegion.deviceAddress     = missIdx.empty()     ? 0 : (sbtBase + missOffset);
        pe.missRegion.stride            = handleSizeAligned;
        pe.missRegion.size              = missSize;
        pe.hitRegion.deviceAddress      = hitIdx.empty()      ? 0 : (sbtBase + hitOffset);
        pe.hitRegion.stride             = handleSizeAligned;
        pe.hitRegion.size               = hitSize;
        pe.callableRegion.deviceAddress = callableIdx.empty() ? 0 : (sbtBase + callableOffset);
        pe.callableRegion.stride        = callableIdx.empty() ? 0 : handleSizeAligned;
        pe.callableRegion.size          = callableSize;

        m_pipelines.emplace(id, pe);
        return true;
    }
#endif // RENDERER_ENABLE_RAY_TRACING

    void VKDevice::DeletePipelineImpl(uint64_t id)
    {
        auto it = m_pipelines.find(id);
        if (it == m_pipelines.end()) return;
        if (it->second.pipeline) vkDestroyPipeline(m_context->GetDevice(), it->second.pipeline, nullptr);
        if (it->second.layout)   vkDestroyPipelineLayout(m_context->GetDevice(), it->second.layout, nullptr);
        for (auto& sl : it->second.setLayouts)
            if (sl) vkDestroyDescriptorSetLayout(m_context->GetDevice(), sl, nullptr);
        // 仅当 compat RP 由 pipeline 拥有时才释放（swapchain 默认 RP 共享，不在此释放）
        if (it->second.ownsCompatRenderPass && it->second.compatRenderPass)
            vkDestroyRenderPass(m_context->GetDevice(), it->second.compatRenderPass, nullptr);
#if defined(RENDERER_ENABLE_RAY_TRACING)
        // 光追管线：一并释放 SBT buffer/memory。
        if (it->second.sbtBuffer) vkDestroyBuffer(m_context->GetDevice(), it->second.sbtBuffer, nullptr);
        if (it->second.sbtMemory) vkFreeMemory(m_context->GetDevice(), it->second.sbtMemory, nullptr);
#endif
        m_pipelines.erase(it);
    }

    // ------------------------------------------------------------------------
    // RenderTarget
    //   - 根据 colorAttachments + depthStencilAttachment 创建多 attachment
    //     VkRenderPass + VkFramebuffer，attachment view 复用各 VKTexture.defaultView。
    //   - 颜色 attachment 的 finalLayout 设为 SHADER_READ_ONLY_OPTIMAL，方便后续
    //     Pass 直接采样；EndRenderPass 时同步更新 currentLayout（见 VKCommandList）。
    //   - 深度 attachment 的 finalLayout 设为 DEPTH_STENCIL_ATTACHMENT_OPTIMAL，
    //     若上层 GBuffer 深度需要被后续 Pass 采样，请改 RT 使用方式（这里保持
    //     最小改动，让 Sponza/RSM 流程能跑通）。
    // ------------------------------------------------------------------------
    bool VKDevice::CreateRenderTargetImpl(uint64_t id, const RenderTargetDesc& desc)
    {
        if (!m_context) return false;

        VKRenderTargetEntry rt{};
        rt.extent.width  = desc.width;
        rt.extent.height = desc.height;

        // 1) 收集 attachment：先 N 个 color，再可选 depth
        std::vector<VkAttachmentDescription> attDescs;
        std::vector<VkAttachmentReference>   colorRefs;
        VkAttachmentReference                depthRef{};
        bool                                 hasDepth = false;

        std::vector<VkImageView> views;  // 用于创建 framebuffer
        views.reserve(desc.colorAttachments.size() + 1);

        for (size_t i = 0; i < desc.colorAttachments.size(); ++i)
        {
            const auto& a = desc.colorAttachments[i];
            const VKTextureEntry* te = LookupTexture(a.texture);
            if (!te || te->defaultView == VK_NULL_HANDLE) continue;

            const uint32_t samples = te->samples ? te->samples : 1;
            VkAttachmentDescription d{};
            d.format         = te->format;
            d.samples        = static_cast<VkSampleCountFlagBits>(samples);
            d.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 由 BeginRenderPass 实际语义决定，这里用 CLEAR 兼容多数路径
            d.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            d.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            d.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            // MSAA 附件不能直接 Sampled，finalLayout 走 TransferSrc 供 ResolveTexture。
            d.finalLayout    = (samples > 1)
                ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attDescs.push_back(d);

            VkAttachmentReference ref{};
            ref.attachment = static_cast<uint32_t>(attDescs.size() - 1);
            ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs.push_back(ref);

            views.push_back(te->defaultView);
            rt.colorAttachments.push_back(a.texture);
        }

        if (desc.depthStencilAttachment.texture.IsValid())
        {
            const VKTextureEntry* te = LookupTexture(desc.depthStencilAttachment.texture);
            if (te && te->defaultView != VK_NULL_HANDLE)
            {
                const uint32_t samples = te->samples ? te->samples : 1;
                VkAttachmentDescription d{};
                d.format         = te->format;
                d.samples        = static_cast<VkSampleCountFlagBits>(samples);
                d.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                d.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                d.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                d.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                d.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                attDescs.push_back(d);

                depthRef.attachment = static_cast<uint32_t>(attDescs.size() - 1);
                depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                hasDepth = true;

                views.push_back(te->defaultView);
                rt.depthStencilAttachment = desc.depthStencilAttachment.texture;
            }
        }

        if (attDescs.empty())
        {
            LOG_STREAM_ERROR("VKDevice") << "CreateRenderTarget: no valid attachments";
            return false;
        }

        // 2) 单 subpass
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = static_cast<uint32_t>(colorRefs.size());
        subpass.pColorAttachments       = colorRefs.empty() ? nullptr : colorRefs.data();
        subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        // 3) 外部依赖：1-sample 供下一 Pass 采样；MSAA 供 ResolveTexture 读。
        const bool isMsaa = !attDescs.empty() && attDescs[0].samples != VK_SAMPLE_COUNT_1_BIT;
        VkSubpassDependency deps[2] = {};
        deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass    = 0;
        deps[0].srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                              | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = 0;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                              | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        deps[1].srcSubpass    = 0;
        deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (isMsaa)
        {
            deps[1].dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        }
        else
        {
            deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                  | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpci{};
        rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = static_cast<uint32_t>(attDescs.size());
        rpci.pAttachments    = attDescs.data();
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;
        rpci.dependencyCount = 2;
        rpci.pDependencies   = deps;
        VK_CHECK(vkCreateRenderPass(m_context->GetDevice(), &rpci, nullptr, &rt.renderPass));

        // 4) Framebuffer
        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = rt.renderPass;
        fbci.attachmentCount = static_cast<uint32_t>(views.size());
        fbci.pAttachments    = views.data();
        fbci.width           = desc.width;
        fbci.height          = desc.height;
        fbci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(m_context->GetDevice(), &fbci, nullptr, &rt.framebuffer));

        m_renderTargets.emplace(id, rt);
        return true;
    }

    // ------------------------------------------------------------------------
    // 仅供 graphics pipeline 烘焙时用的"compatibility RenderPass"。
    //   VK 要求 pipeline.renderPass 与实际 BeginRenderPass 用的 RenderPass
    //   compatible（attachment count + format + samples 一致）。这里按 rtLayout
    //   构造一个最小 RP；loadOp/storeOp/initialLayout/finalLayout 不影响兼容性。
    // ------------------------------------------------------------------------
    VkRenderPass VKDevice::CreateCompatibleRenderPass(const RenderTargetLayout& rtLayout)
    {
        if (!m_context) return VK_NULL_HANDLE;

        std::vector<VkAttachmentDescription> attDescs;
        std::vector<VkAttachmentReference>   colorRefs;
        VkAttachmentReference                depthRef{};
        bool                                 hasDepth = false;

        for (size_t i = 0; i < rtLayout.colorFormats.size(); ++i)
        {
            VkAttachmentDescription d{};
            d.format         = ToVkFormat(rtLayout.colorFormats[i]);
            d.samples        = static_cast<VkSampleCountFlagBits>(rtLayout.samples ? rtLayout.samples : 1);
            d.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            d.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            d.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            d.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            d.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attDescs.push_back(d);

            VkAttachmentReference ref{};
            ref.attachment = static_cast<uint32_t>(attDescs.size() - 1);
            ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs.push_back(ref);
        }

        if (rtLayout.depthStencilFormat != Format::Undefined)
        {
            VkAttachmentDescription d{};
            d.format         = ToVkFormat(rtLayout.depthStencilFormat);
            d.samples        = static_cast<VkSampleCountFlagBits>(rtLayout.samples ? rtLayout.samples : 1);
            d.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            d.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            d.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            d.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            d.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attDescs.push_back(d);

            depthRef.attachment = static_cast<uint32_t>(attDescs.size() - 1);
            depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            hasDepth = true;
        }

        if (attDescs.empty()) return VK_NULL_HANDLE;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = static_cast<uint32_t>(colorRefs.size());
        subpass.pColorAttachments       = colorRefs.empty() ? nullptr : colorRefs.data();
        subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        // 修复：Vulkan 1.4 spec 的 RenderPass Compatibility 规则要求两端
        // RenderPass 的 dependencyCount 也必须相等（VUID-vkCmdDrawIndexed-renderPass-02684
        // 关联章节）。CreateRenderTargetImpl 中给 RT 的 RenderPass 加了 2 个
        // SubpassDependency（external↔subpass），这里 compatibility RP 必须保持一致，
        // 否则 Validation 会报：
        //   "dependencyCount is incompatible between VkRenderPass A (cmd buffer)
        //    and VkRenderPass B (pipeline), 2 != 0."
        VkSubpassDependency deps[2] = {};
        deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass    = 0;
        deps[0].srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                              | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = 0;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                              | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        deps[1].srcSubpass    = 0;
        deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (rtLayout.samples > 1)
        {
            deps[1].dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        }
        else
        {
            deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                  | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpci{};
        rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = static_cast<uint32_t>(attDescs.size());
        rpci.pAttachments    = attDescs.data();
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;
        rpci.dependencyCount = 2;
        rpci.pDependencies   = deps;

        VkRenderPass rp = VK_NULL_HANDLE;
        if (vkCreateRenderPass(m_context->GetDevice(), &rpci, nullptr, &rp) != VK_SUCCESS)
        {
            LOG_STREAM_ERROR("VKDevice") << "CreateCompatibleRenderPass failed";
            return VK_NULL_HANDLE;
        }
        return rp;
    }

    void VKDevice::DeleteRenderTargetImpl(uint64_t id)
    {
        auto it = m_renderTargets.find(id);
        if (it == m_renderTargets.end()) return;
        if (it->second.framebuffer) vkDestroyFramebuffer(m_context->GetDevice(), it->second.framebuffer, nullptr);
        if (it->second.renderPass)  vkDestroyRenderPass (m_context->GetDevice(), it->second.renderPass,  nullptr);
        m_renderTargets.erase(it);
    }

    // ------------------------------------------------------------------------
    // 帧控制 *Impl()：把 vkAcquireNextImageKHR / vkQueueSubmit / vkQueuePresentKHR
    // 等同步细节封装到内部。
    // 帧索引推进由基类 GDevice::Present() 在 PresentImpl 后统一处理。
    // ------------------------------------------------------------------------
    void VKDevice::BeginFrameImpl()
    {
        if (!m_context || m_frames.empty()) return;
        auto& frame = m_frames[m_currentFrameIndex];

        // 等待上一帧
        {
            ZoneScopedN("VK::WaitInFlightFence");
            vkWaitForFences(m_context->GetDevice(), 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
        }

        // 获取 swapchain image
        VkResult acq = VK_SUCCESS;
        {
            ZoneScopedN("VK::AcquireNextImage");
            acq = vkAcquireNextImageKHR(
                m_context->GetDevice(), m_swapchain->GetSwapchain(),
                UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &m_currentImageIndex);
        }
        if (acq == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_swapchain->Recreate(*m_context, *m_windowPtr);
            m_frameInProgress = false;
            return;
        }
        else if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
        {
            LOG_STREAM_ERROR("VKDevice") << "AcquireNextImage failed";
            m_frameInProgress = false;
            return;
        }

        vkResetFences(m_context->GetDevice(), 1, &frame.inFlightFence);

        // WaitFence 之后本 FIF 槽位上的 DS/CB 已不被 GPU 使用，可跨帧复用
        // DescriptorSet（见 VKCommandList per-frame cache），不再每帧
        // vkResetDescriptorPool。池耗尽时由 AllocateDescriptorSet 懒重置。
        frame.primaryCmd->Reset();

        // Begin Primary CmdBuffer（CB 进入 RECORDING 状态）
        frame.primaryCmd->Begin();

        // 让 VKCommandList 接管该 CmdBuffer（保留本槽位 DS 内容缓存）
        m_commandList->Reset(frame.primaryCmd->Get(), m_currentFrameIndex);
        m_frameInProgress = true;
    }

    RenderCommandList* VKDevice::AcquireCommandListImpl()
    {
        return m_commandList.get();
    }

    void VKDevice::SubmitImpl(RenderCommandList* /*cmd*/)
    {
        if (!m_frameInProgress || m_frames.empty()) return;
        auto& frame = m_frames[m_currentFrameIndex];

        // 修复：业务 Pass 录制完成、End() 之前，把 imgui draw 也
        // 录入同一 primaryCmd（独立 RenderPass，loadOp=LOAD 叠加在场景之上）。
        // 必须在 End() 之前，否则 cmdbuf 已关闭无法再录命令。
        RecordImGuiOverlayInPrimaryCmd();

        // End Primary CmdBuffer
        frame.primaryCmd->End();

        // Overlay 在下一帧 DrawFrame 之前读取，此处固化本帧 BindResourceSet 统计
        if (m_commandList)
            m_commandList->PublishFrameStats();

        // 提交
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkCommandBuffer cb = frame.primaryCmd->Get();
        // 使用 per-image renderFinished semaphore，避免 framesInFlight < imageCount 时复用
        VkSemaphore renderFinishedSem = VK_NULL_HANDLE;
        if (m_currentImageIndex < m_imageRenderFinishedSemaphores.size())
            renderFinishedSem = m_imageRenderFinishedSemaphores[m_currentImageIndex];

        VkSubmitInfo si{};
        si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = &frame.imageAvailable;
        si.pWaitDstStageMask    = &waitStage;
        si.commandBufferCount   = 1;
        si.pCommandBuffers      = &cb;
        si.signalSemaphoreCount = renderFinishedSem ? 1u : 0u;
        si.pSignalSemaphores    = renderFinishedSem ? &renderFinishedSem : nullptr;
        VK_CHECK(vkQueueSubmit(m_context->GetGraphicsQueue(), 1, &si, frame.inFlightFence));
    }

    bool VKDevice::ReadbackBackbuffer(std::vector<uint8_t>& outRgba,
                                      uint32_t& outWidth,
                                      uint32_t& outHeight)
    {
        if (!m_context || !m_swapchain || !m_frameInProgress || m_frames.empty())
        {
            LOG_STREAM_ERROR("VKDevice") << "ReadbackBackbuffer: device/swapchain not ready";
            return false;
        }

        const VkExtent2D ext = m_swapchain->GetExtent();
        outWidth = ext.width;
        outHeight = ext.height;
        if (outWidth == 0 || outHeight == 0)
        {
            LOG_STREAM_ERROR("VKDevice") << "ReadbackBackbuffer: invalid extent";
            return false;
        }

        VkImage srcImage = m_swapchain->GetImage(m_currentImageIndex);
        if (srcImage == VK_NULL_HANDLE)
        {
            LOG_STREAM_ERROR("VKDevice") << "ReadbackBackbuffer: invalid swapchain image";
            return false;
        }

        // 等待本帧 Submit 完成，再做 one-shot copy（Present 之前调用）。
        auto& frame = m_frames[m_currentFrameIndex];
        if (frame.inFlightFence != VK_NULL_HANDLE)
        {
            VK_CHECK(vkWaitForFences(m_context->GetDevice(), 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX));
        }

        const VkDeviceSize bytes = static_cast<VkDeviceSize>(outWidth) * outHeight * 4u;

        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = bytes;
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        if (vkCreateBuffer(m_context->GetDevice(), &bci, nullptr, &staging) != VK_SUCCESS)
        {
            LOG_STREAM_ERROR("VKDevice") << "ReadbackBackbuffer: create staging buffer failed";
            return false;
        }

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(m_context->GetDevice(), staging, &req);
        VkMemoryAllocateInfo mai{};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = req.size;
        mai.memoryTypeIndex = m_context->FindMemoryType(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_context->GetDevice(), &mai, nullptr, &stagingMemory) != VK_SUCCESS)
        {
            vkDestroyBuffer(m_context->GetDevice(), staging, nullptr);
            LOG_STREAM_ERROR("VKDevice") << "ReadbackBackbuffer: allocate staging memory failed";
            return false;
        }
        VK_CHECK(vkBindBufferMemory(m_context->GetDevice(), staging, stagingMemory, 0));

        VkCommandBuffer cb = BeginOneTimeCommands();
        if (cb == VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_context->GetDevice(), staging, nullptr);
            vkFreeMemory(m_context->GetDevice(), stagingMemory, nullptr);
            return false;
        }

        // default / ImGui RP 的 finalLayout 均为 PRESENT_SRC_KHR
        {
            VkImageMemoryBarrier b{};
            b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout                       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            b.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            b.image                           = srcImage;
            b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.baseMipLevel   = 0;
            b.subresourceRange.levelCount     = 1;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount     = 1;
            b.srcAccessMask                   = VK_ACCESS_MEMORY_READ_BIT;
            b.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }

        {
            VkBufferImageCopy region{};
            region.bufferOffset                    = 0;
            region.bufferRowLength                 = 0;
            region.bufferImageHeight               = 0;
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount     = 1;
            region.imageOffset                     = {0, 0, 0};
            region.imageExtent                     = {outWidth, outHeight, 1};
            vkCmdCopyImageToBuffer(cb, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   staging, 1, &region);
        }

        {
            VkImageMemoryBarrier b{};
            b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            b.newLayout                       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            b.image                           = srcImage;
            b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.baseMipLevel   = 0;
            b.subresourceRange.levelCount     = 1;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount     = 1;
            b.srcAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
            b.dstAccessMask                   = 0;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }

        EndOneTimeCommands(cb);

        void* mapped = nullptr;
        if (vkMapMemory(m_context->GetDevice(), stagingMemory, 0, bytes, 0, &mapped) != VK_SUCCESS
            || !mapped)
        {
            vkDestroyBuffer(m_context->GetDevice(), staging, nullptr);
            vkFreeMemory(m_context->GetDevice(), stagingMemory, nullptr);
            LOG_STREAM_ERROR("VKDevice") << "ReadbackBackbuffer: map staging failed";
            return false;
        }

        const auto* src = static_cast<const uint8_t*>(mapped);
        outRgba.resize(static_cast<size_t>(bytes));
        const VkFormat fmt = m_swapchain->GetImageFormat();
        // 常见 swapchain：B8G8R8A8_* → 交换 R/B；其余按 RGBA 直拷。
        if (fmt == VK_FORMAT_B8G8R8A8_SRGB || fmt == VK_FORMAT_B8G8R8A8_UNORM)
        {
            for (VkDeviceSize i = 0; i < bytes; i += 4)
            {
                outRgba[static_cast<size_t>(i) + 0] = src[static_cast<size_t>(i) + 2];
                outRgba[static_cast<size_t>(i) + 1] = src[static_cast<size_t>(i) + 1];
                outRgba[static_cast<size_t>(i) + 2] = src[static_cast<size_t>(i) + 0];
                outRgba[static_cast<size_t>(i) + 3] = src[static_cast<size_t>(i) + 3];
            }
        }
        else
        {
            std::memcpy(outRgba.data(), src, static_cast<size_t>(bytes));
            if (fmt != VK_FORMAT_R8G8B8A8_SRGB && fmt != VK_FORMAT_R8G8B8A8_UNORM)
            {
                LOG_STREAM_WARN("VKDevice")
                    << "ReadbackBackbuffer: unhandled swapchain format "
                    << static_cast<int>(fmt) << ", copied as RGBA bytes";
            }
        }

        vkUnmapMemory(m_context->GetDevice(), stagingMemory);
        vkDestroyBuffer(m_context->GetDevice(), staging, nullptr);
        vkFreeMemory(m_context->GetDevice(), stagingMemory, nullptr);
        return true;
    }

    void VKDevice::PresentImpl()
    {
        if (!m_frameInProgress || m_frames.empty()) return;
        auto& frame = m_frames[m_currentFrameIndex];

        VkSwapchainKHR swapchains[] = { m_swapchain->GetSwapchain() };
        VkSemaphore renderFinishedSem = VK_NULL_HANDLE;
        if (m_currentImageIndex < m_imageRenderFinishedSemaphores.size())
            renderFinishedSem = m_imageRenderFinishedSemaphores[m_currentImageIndex];

        VkPresentInfoKHR pi{};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = renderFinishedSem ? 1u : 0u;
        pi.pWaitSemaphores    = renderFinishedSem ? &renderFinishedSem : nullptr;
        pi.swapchainCount     = 1;
        pi.pSwapchains        = swapchains;
        pi.pImageIndices      = &m_currentImageIndex;

        VkResult res = vkQueuePresentKHR(m_context->GetPresentQueue(), &pi);
        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR
            || (m_windowPtr && m_windowPtr->IsResized()))
        {
            if (m_windowPtr) m_windowPtr->ClearResizedFlag();
            m_swapchain->Recreate(*m_context, *m_windowPtr);
        }
        else if (res != VK_SUCCESS)
        {
            LOG_STREAM_ERROR("VKDevice") << "QueuePresent failed";
        }

        m_frameInProgress = false;
        // 帧索引推进由基类 Present() 统一处理。
    }

    // ------------------------------------------------------------------------
    // ImGui Overlay Hook 实现
    // ------------------------------------------------------------------------
    void VKDevice::SetImGuiOverlayCallback(ImGuiOverlayCallback cb, void* userData)
    {
        m_imGuiCallback = cb;
        m_imGuiUserData = userData;
    }

    VkCommandBuffer VKDevice::GetCurrentPrimaryCommandBuffer() const
    {
        if (!m_frameInProgress || m_frames.empty()) return VK_NULL_HANDLE;
        const auto& frame = m_frames[m_currentFrameIndex];
        return frame.primaryCmd ? frame.primaryCmd->Get() : VK_NULL_HANDLE;
    }

    VkCommandBuffer VKDevice::ImGuiBeginOneTimeCommands()
    {
        return BeginOneTimeCommands();
    }

    void VKDevice::ImGuiEndOneTimeCommands(VkCommandBuffer cb)
    {
        EndOneTimeCommands(cb);
    }

    void VKDevice::RenderImGuiOverlay()
    {
        // 修复：VK 后端的 imgui draw 必须录在 primaryCmd 内，但
        // PassScheduler 调用本接口的时机在 Submit 之后，那时 primaryCmd
        // 已经 End。因此 VK 端真正的录制逻辑挪到了 SubmitImpl 内、
        // primaryCmd->End() 之前（参见 RecordImGuiOverlayInPrimaryCmd）。
        // 这里保留一个空实现，避免 PassScheduler 调用空指针。
    }

    void VKDevice::RecordImGuiOverlayInPrimaryCmd()
    {
        if (!m_imGuiCallback) return;
        if (!m_frameInProgress || m_frames.empty()) return;
        if (!m_swapchain || !m_context) return;

        auto& frame = m_frames[m_currentFrameIndex];
        if (!frame.primaryCmd) return;
        VkCommandBuffer cb = frame.primaryCmd->Get();
        if (cb == VK_NULL_HANDLE) return;

        // 修复：使用 swapchain 的 ImGui Overlay 专用 RenderPass
        //   - color attachment loadOp=LOAD（保留 ScreenQuadPass 的输出，不再清成黑屏）
        //   - initialLayout=PRESENT_SRC_KHR（接续 default RP 的 finalLayout）
        //   - finalLayout=PRESENT_SRC_KHR（直接交给 Present）
        //   - 单 attachment（无 depth），imgui 不需要深度测试
        VkRenderPass  rp  = m_swapchain->GetImGuiRenderPass();
        VkFramebuffer fb  = m_swapchain->GetImGuiFramebuffer(m_currentImageIndex);
        VkExtent2D    ext = m_swapchain->GetExtent();
        if (rp == VK_NULL_HANDLE || fb == VK_NULL_HANDLE) return;

        VkRenderPassBeginInfo rpbi{};
        rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass        = rp;
        rpbi.framebuffer       = fb;
        rpbi.renderArea.offset = {0, 0};
        rpbi.renderArea.extent = ext;
        rpbi.clearValueCount   = 0;       // loadOp=LOAD 不需要 clearValue
        rpbi.pClearValues      = nullptr;

        vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        m_imGuiCallback(m_imGuiUserData);
        vkCmdEndRenderPass(cb);
    }

    // ------------------------------------------------------------------------
    // 资源查询
    // ------------------------------------------------------------------------
    const VKBufferEntry* VKDevice::LookupBuffer(BufferHandle h) const
    {
        auto it = m_buffers.find(h.id);
        return it == m_buffers.end() ? nullptr : &it->second;
    }
    const VKTextureEntry* VKDevice::LookupTexture(TextureHandle h) const
    {
        auto it = m_textures.find(h.id);
        return it == m_textures.end() ? nullptr : &it->second;
    }
    VKTextureEntry* VKDevice::MutableLookupTexture(TextureHandle h)
    {
        auto it = m_textures.find(h.id);
        return it == m_textures.end() ? nullptr : &it->second;
    }
    const VKSamplerEntry* VKDevice::LookupSampler(SamplerHandle h) const
    {
        auto it = m_samplers.find(h.id);
        return it == m_samplers.end() ? nullptr : &it->second;
    }
    const VKShaderEntry* VKDevice::LookupShader(ShaderHandle h) const
    {
        auto it = m_shaders.find(h.id);
        return it == m_shaders.end() ? nullptr : &it->second;
    }
    const VKPipelineEntry* VKDevice::LookupPipeline(PipelineHandle h) const
    {
        auto it = m_pipelines.find(h.id);
        return it == m_pipelines.end() ? nullptr : &it->second;
    }
    const VKRenderTargetEntry* VKDevice::LookupRenderTarget(RenderTargetHandle h) const
    {
        auto it = m_renderTargets.find(h.id);
        return it == m_renderTargets.end() ? nullptr : &it->second;
    }

    // ------------------------------------------------------------------------
    // DescriptorPool 池化（per-frame，跨帧复用）
    //   策略：每帧一个 pool；与 CommandList 的 per-frame DS 内容缓存一起，在
    //   WaitFence 后复用已分配的 DescriptorSet，避免每帧 Allocate+Update。
    //   仅当 Allocate 失败（池耗尽）时整池 reset 并清空该槽位缓存。
    // ------------------------------------------------------------------------
    void VKDevice::CreateDescriptorPools(uint32_t framesInFlight)
    {
        // Sponza 稳定场景下唯一 DS 约等于材质量级（连续同 Diffuse 合并后），
        // 但首帧/缓存失效时仍可能冲高。取 4096 作为安全上限。
        constexpr uint32_t kMaxSetsPerFrame = 4096;
        constexpr uint32_t kPerTypeCount    = 4096;

        const VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         kPerTypeCount },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         kPerTypeCount },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kPerTypeCount },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          kPerTypeCount },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          kPerTypeCount },
            { VK_DESCRIPTOR_TYPE_SAMPLER,                kPerTypeCount },
        };

        for (uint32_t i = 0; i < framesInFlight && i < m_frames.size(); ++i)
        {
            VkDescriptorPoolCreateInfo ci{};
            ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            ci.flags         = 0;   // 不需要 FREE_DESCRIPTOR_SET_BIT —— 整池 reset
            ci.maxSets       = kMaxSetsPerFrame;
            ci.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
            ci.pPoolSizes    = sizes;
            VK_CHECK(vkCreateDescriptorPool(m_context->GetDevice(), &ci, nullptr,
                                            &m_frames[i].descriptorPool));
        }
    }

    void VKDevice::DestroyDescriptorPools()
    {
        if (m_commandList)
            m_commandList->InvalidateDescriptorCaches();
        if (!m_context) return;
        for (auto& f : m_frames)
        {
            if (f.descriptorPool)
            {
                vkDestroyDescriptorPool(m_context->GetDevice(), f.descriptorPool, nullptr);
                f.descriptorPool = VK_NULL_HANDLE;
            }
        }
    }

    VkDescriptorSet VKDevice::AllocateDescriptorSet(VkDescriptorSetLayout layout)
    {
        if (!layout || m_frames.empty()) return VK_NULL_HANDLE;
        auto& frame = m_frames[m_currentFrameIndex];
        if (!frame.descriptorPool) return VK_NULL_HANDLE;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = frame.descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult r = vkAllocateDescriptorSets(m_context->GetDevice(), &ai, &set);
        if (r != VK_SUCCESS)
        {
            // 池耗尽：仅回收当前 FIF 槽位并丢掉对应缓存后重试一次
            if (m_commandList)
                m_commandList->InvalidateDescriptorCache(m_currentFrameIndex);
            vkResetDescriptorPool(m_context->GetDevice(), frame.descriptorPool, 0);
            set = VK_NULL_HANDLE;
            r = vkAllocateDescriptorSets(m_context->GetDevice(), &ai, &set);
        }
        if (r != VK_SUCCESS)
        {
            LOG_STREAM_ERROR("VKDevice") << "AllocateDescriptorSet failed (vk=" << r
                      << "); pool may be exhausted";
            return VK_NULL_HANDLE;
        }
        return set;
    }

    // ------------------------------------------------------------------------
    // 通用 immediate one-shot CmdBuffer 基础设施。
    // 走 graphicsQueue + vkQueueWaitIdle 模式，资源初始化期专用。
    // ------------------------------------------------------------------------
    VkCommandBuffer VKDevice::BeginOneTimeCommands()
    {
        if (!m_context || !m_commandPool) return VK_NULL_HANDLE;

        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = m_commandPool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;

        VkCommandBuffer cb = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(m_context->GetDevice(), &ai, &cb));

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cb, &bi));
        return cb;
    }

    void VKDevice::EndOneTimeCommands(VkCommandBuffer cb)
    {
        if (cb == VK_NULL_HANDLE) return;

        VK_CHECK(vkEndCommandBuffer(cb));

        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        VK_CHECK(vkQueueSubmit(m_context->GetGraphicsQueue(), 1, &si, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(m_context->GetGraphicsQueue()));

        vkFreeCommandBuffers(m_context->GetDevice(), m_commandPool, 1, &cb);
    }

    // ------------------------------------------------------------------------
    // immediate one-shot CmdBuffer 做 image layout 转换
    //   仅供 Storage Image 创建时的 UNDEFINED→GENERAL 这一类初始化路径使用。
    //   走 graphicsQueue + vkQueueWaitIdle，避免污染 frame in flight 同步。
    // ------------------------------------------------------------------------
    void VKDevice::TransitionImageLayoutImmediate(VkImage image,
                                                  VkImageAspectFlags aspect,
                                                  uint32_t mipLevels,
                                                  uint32_t arrayLayers,
                                                  VkImageLayout oldLayout,
                                                  VkImageLayout newLayout)
    {
        if (image == VK_NULL_HANDLE) return;

        VkCommandBuffer cb = BeginOneTimeCommands();
        if (cb == VK_NULL_HANDLE) return;

        VkImageMemoryBarrier b{};
        b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout                       = oldLayout;
        b.newLayout                       = newLayout;
        b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.image                           = image;
        b.subresourceRange.aspectMask     = aspect;
        b.subresourceRange.baseMipLevel   = 0;
        b.subresourceRange.levelCount     = mipLevels;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount     = arrayLayers;
        // 简化 access mask：UNDEFINED→GENERAL 不要求严格语义
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cb,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);

        EndOneTimeCommands(cb);
    }

    // ------------------------------------------------------------------------
    // 把 host 数据经 staging buffer 拷到 device-local buffer。
    // ------------------------------------------------------------------------
    void VKDevice::UploadBufferViaStaging(VkBuffer dstBuffer,
                                          VkDeviceSize dstOffset,
                                          const void* src,
                                          VkDeviceSize bytes)
    {
        if (!m_context || dstBuffer == VK_NULL_HANDLE || !src || bytes == 0) return;

        // 1) 创建 host-visible staging buffer
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = bytes;
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer       staging       = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        VK_CHECK(vkCreateBuffer(m_context->GetDevice(), &bci, nullptr, &staging));

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(m_context->GetDevice(), staging, &req);

        VkMemoryAllocateInfo mai{};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = req.size;
        mai.memoryTypeIndex = m_context->FindMemoryType(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK(vkAllocateMemory(m_context->GetDevice(), &mai, nullptr, &stagingMemory));
        VK_CHECK(vkBindBufferMemory(m_context->GetDevice(), staging, stagingMemory, 0));

        // 2) memcpy 数据进 staging
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(m_context->GetDevice(), stagingMemory, 0, bytes, 0, &mapped));
        std::memcpy(mapped, src, static_cast<size_t>(bytes));
        vkUnmapMemory(m_context->GetDevice(), stagingMemory);

        // 3) one-shot CmdBuffer：staging → dstBuffer
        VkCommandBuffer cb = BeginOneTimeCommands();
        if (cb != VK_NULL_HANDLE)
        {
            VkBufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = dstOffset;
            region.size      = bytes;
            vkCmdCopyBuffer(cb, staging, dstBuffer, 1, &region);
            EndOneTimeCommands(cb);
        }

        // 4) 释放 staging
        vkDestroyBuffer(m_context->GetDevice(), staging, nullptr);
        vkFreeMemory(m_context->GetDevice(), stagingMemory, nullptr);
    }

    // ------------------------------------------------------------------------
    // 把 host 像素数据经 staging buffer 拷到 image 指定 subresource。
    //   layout 流程：UNDEFINED → TRANSFER_DST_OPTIMAL → vkCmdCopyBufferToImage
    //                → finalLayout（一般是 SHADER_READ_ONLY_OPTIMAL）。
    // ------------------------------------------------------------------------
    void VKDevice::UploadImageViaStaging(VkImage image,
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
                                         VkImageLayout finalLayout)
    {
        if (!m_context || image == VK_NULL_HANDLE || !src || bytes == 0) return;

        // 1) 创建 staging
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = bytes;
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer       staging       = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        VK_CHECK(vkCreateBuffer(m_context->GetDevice(), &bci, nullptr, &staging));

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(m_context->GetDevice(), staging, &req);

        VkMemoryAllocateInfo mai{};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = req.size;
        mai.memoryTypeIndex = m_context->FindMemoryType(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK(vkAllocateMemory(m_context->GetDevice(), &mai, nullptr, &stagingMemory));
        VK_CHECK(vkBindBufferMemory(m_context->GetDevice(), staging, stagingMemory, 0));

        // 2) memcpy
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(m_context->GetDevice(), stagingMemory, 0, bytes, 0, &mapped));
        std::memcpy(mapped, src, static_cast<size_t>(bytes));
        vkUnmapMemory(m_context->GetDevice(), stagingMemory);

        // 3) 走一个 cmd buffer 完成所有 layout 转换 + copy
        VkCommandBuffer cb = BeginOneTimeCommands();
        if (cb != VK_NULL_HANDLE)
        {
            // 3.1) UNDEFINED → TRANSFER_DST_OPTIMAL（仅指定 subresource）
            {
                VkImageMemoryBarrier b{};
                b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
                b.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.image                           = image;
                b.subresourceRange.aspectMask     = aspect;
                b.subresourceRange.baseMipLevel   = mipLevel;
                b.subresourceRange.levelCount     = 1;
                b.subresourceRange.baseArrayLayer = arrayLayer;
                b.subresourceRange.layerCount     = 1;
                b.srcAccessMask                   = 0;
                b.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(cb,
                                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &b);
            }

            // 3.2) vkCmdCopyBufferToImage
            {
                VkBufferImageCopy region{};
                region.bufferOffset                    = 0;
                region.bufferRowLength                 = 0;
                region.bufferImageHeight               = 0;
                region.imageSubresource.aspectMask     = aspect;
                region.imageSubresource.mipLevel       = mipLevel;
                region.imageSubresource.baseArrayLayer = arrayLayer;
                region.imageSubresource.layerCount     = 1;
                region.imageOffset                     = { 0, 0, 0 };
                region.imageExtent                     = { width, height, depth ? depth : 1 };
                vkCmdCopyBufferToImage(cb, staging, image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       1, &region);
            }

            // 3.3) TRANSFER_DST_OPTIMAL → finalLayout（仅 mipLevel 这一层）
            {
                VkImageMemoryBarrier b{};
                b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                b.newLayout                       = finalLayout;
                b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.image                           = image;
                b.subresourceRange.aspectMask     = aspect;
                b.subresourceRange.baseMipLevel   = mipLevel;
                b.subresourceRange.levelCount     = 1;
                b.subresourceRange.baseArrayLayer = arrayLayer;
                b.subresourceRange.layerCount     = 1;
                b.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
                b.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cb,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                       | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &b);
            }

            // 3.4) 把未上传的 mip level（1..mipCount-1）从 UNDEFINED 转到 finalLayout，
            //      避免 Validation Layer 报 "expects SHADER_READ_ONLY but layout is UNDEFINED"。
            //      这些 mip level 没有实际数据，但 shader 采样时 Vulkan 要求 layout 一致。
            if (mipCount > 1 && mipLevel == 0)
            {
                VkImageMemoryBarrier b{};
                b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
                b.newLayout                       = finalLayout;
                b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.image                           = image;
                b.subresourceRange.aspectMask     = aspect;
                b.subresourceRange.baseMipLevel   = 1;
                b.subresourceRange.levelCount     = mipCount - 1;
                b.subresourceRange.baseArrayLayer = 0;
                b.subresourceRange.layerCount     = layerCount ? layerCount : 1;
                b.srcAccessMask                   = 0;
                b.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cb,
                                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                       | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &b);
            }

            EndOneTimeCommands(cb);
        }

        // 4) 释放 staging
        vkDestroyBuffer(m_context->GetDevice(), staging, nullptr);
        vkFreeMemory(m_context->GetDevice(), stagingMemory, nullptr);
        // 注意：调用方需自行更新 VKTextureEntry::currentLayout 以反映 finalLayout。
        (void)mipCount;
        (void)layerCount;
    }
}
