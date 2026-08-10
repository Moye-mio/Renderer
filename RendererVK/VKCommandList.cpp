// ============================================================================
// RendererVK - VKCommandList.cpp
// 把后端无关的 RenderCommandList 调用翻译为 Vulkan 的 vkCmdXxx。
// 注意：本类不负责 vkBegin/EndCommandBuffer，那由 VKDevice 在 BeginFrame/Submit
// 内部完成。
// ============================================================================
#include "VKCommandList.h"
#include "VKDevice.h"
#include "VKTranslate.h"
#include "VkContext.h"
#include "VkSwapchainWrapper.h"
#include "Common.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include "Logger.h"


namespace TitusVkGraphics
{
    using namespace TitusRHI;

    VKCommandList::VKCommandList(VKDevice* device)
        : m_device(device)
    {
    }

    VKCommandList::DsContentKey
    VKCommandList::MakeContentKey(VkDescriptorSetLayout layout,
                                  const TitusRHI::ResourceSetDesc& desc)
    {
        DsContentKey key;
        key.layout = layout;
        key.slots.reserve(desc.bindings.size());
        for (const auto& b : desc.bindings)
        {
            DsContentKey::Slot s{};
            s.binding = b.binding;
            s.type    = static_cast<uint32_t>(b.type);
            switch (b.type)
            {
            case ResourceBindingType::UniformBuffer:
            case ResourceBindingType::StorageBuffer:
                s.id0    = b.buffer.id;
                s.offset = b.bufferOffset;
                s.range  = b.bufferRange;
                break;
            case ResourceBindingType::SampledTexture:
            case ResourceBindingType::StorageTexture:
                s.id0 = b.texture.id;
                break;
            case ResourceBindingType::CombinedImageSampler:
                s.id0 = b.texture.id;
                s.id1 = b.sampler.id;
                break;
            case ResourceBindingType::Sampler:
                s.id0 = b.sampler.id;
                break;
#if defined(RENDERER_ENABLE_RAY_TRACING)
            case ResourceBindingType::AccelerationStructure:
                s.id0 = b.accelStruct.id;
                break;
#endif
            default:
                break;
            }
            key.slots.push_back(s);
        }
        std::sort(key.slots.begin(), key.slots.end(),
                  [](const DsContentKey::Slot& a, const DsContentKey::Slot& b)
                  { return a.binding < b.binding; });
        return key;
    }

    void VKCommandList::Reset(VkCommandBuffer cmd)
    {
        m_frameStats = {};

        m_cmd = cmd;
        m_currentPipeline = VK_NULL_HANDLE;
        m_currentLayout   = VK_NULL_HANDLE;
        m_currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        m_currentPipelineEntry = nullptr;
        // 每帧重置：pool 整池 reset 后旧 DS 失效，内容缓存必须清空
        m_boundSets.fill(VK_NULL_HANDLE);
        m_dsContentCache.clear();
        for (auto& s : m_setStateCache) s = TitusRHI::ResourceSetDesc{};
    }

    void VKCommandList::PublishFrameStats()
    {
        m_lastFrameStats = m_frameStats;
        // 排查用：首个非空帧打一次，确认 DS alloc 已从 ~SubMesh 数降到材质量级
        static bool s_loggedOnce = false;
        if (!s_loggedOnce && m_frameStats.bindResourceSetCalls > 0)
        {
            s_loggedOnce = true;
            LOG_STREAM_INFO("VKCommandList")
                << "BindResourceSet stats (first frame): calls="
                << m_frameStats.bindResourceSetCalls
                << " allocs=" << m_frameStats.descriptorAllocs
                << " cacheHits=" << m_frameStats.descriptorCacheHits;
            TitusBasic::Logger::Instance().Flush();
        }
    }

    // ------------------------------------------------------------------------
    // RenderPass
    // ------------------------------------------------------------------------
    void VKCommandList::BeginRenderPass(const RenderPassBeginInfo& info)
    {
        if (!m_cmd) return;

        VkRenderPass  renderPass  = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D    extent      = { 0, 0 };

        // 保存当前 RT，EndRenderPass 时同步 attachment.currentLayout
        m_activeRenderTarget = info.renderTarget;

        if (!info.renderTarget.IsValid())
        {
            // 默认 backbuffer：使用 Swapchain 内置 RenderPass + 当前帧 Framebuffer
            auto* swap = m_device->GetVkSwapchain();
            assert(swap && "VKDevice swapchain not initialized");
            renderPass  = swap->GetDefaultRenderPass();
            framebuffer = swap->GetFramebuffer(m_device->GetCurrentImageIndex());
            extent      = swap->GetExtent();
        }
        else
        {
            const VKRenderTargetEntry* rt = m_device->LookupRenderTarget(info.renderTarget);
            assert(rt && "Invalid RenderTargetHandle");
            renderPass  = rt->renderPass;
            framebuffer = rt->framebuffer;
            extent      = rt->extent;
        }

        // 收集 ClearValue（Vulkan 一份数组：先 N 个颜色，再可选深度）
        std::vector<VkClearValue> clears;
        clears.reserve(info.colorOps.size() + (info.hasDepthStencil ? 1 : 0));
        for (const auto& op : info.colorOps)
        {
            VkClearValue cv{};
            cv.color = { { op.clearValue.color[0], op.clearValue.color[1],
                           op.clearValue.color[2], op.clearValue.color[3] } };
            clears.push_back(cv);
        }
        if (info.hasDepthStencil)
        {
            VkClearValue cv{};
            cv.depthStencil = { info.depthStencilOp.clearValue.depth,
                                info.depthStencilOp.clearValue.stencil };
            clears.push_back(cv);
        }

        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass        = renderPass;
        rpInfo.framebuffer       = framebuffer;
        rpInfo.renderArea.offset = { info.renderArea.offsetX, info.renderArea.offsetY };
        rpInfo.renderArea.extent.width  = info.renderArea.width  ? info.renderArea.width  : extent.width;
        rpInfo.renderArea.extent.height = info.renderArea.height ? info.renderArea.height : extent.height;
        rpInfo.clearValueCount = static_cast<uint32_t>(clears.size());
        rpInfo.pClearValues    = clears.empty() ? nullptr : clears.data();

        vkCmdBeginRenderPass(m_cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void VKCommandList::EndRenderPass()
    {
        if (!m_cmd) return;
        vkCmdEndRenderPass(m_cmd);

        // 自定义 RT 的 RenderPass.finalLayout 设为 SHADER_READ_ONLY_OPTIMAL，
        // 这里同步更新所有 color attachment 的 currentLayout 跟踪状态，
        // 让后续 Pass 的 BindResourceSet 走 sampled-image 路径不再误判为 GENERAL/UNDEFINED。
        if (m_activeRenderTarget.IsValid())
        {
            const VKRenderTargetEntry* rt = m_device->LookupRenderTarget(m_activeRenderTarget);
            if (rt)
            {
                for (const auto& th : rt->colorAttachments)
                {
                    if (VKTextureEntry* te = m_device->MutableLookupTexture(th))
                    {
                        te->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    }
                }
                if (rt->depthStencilAttachment.IsValid())
                {
                    if (VKTextureEntry* te = m_device->MutableLookupTexture(rt->depthStencilAttachment))
                    {
                        te->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    }
                }
            }
        }
        m_activeRenderTarget = {};
    }

    // ------------------------------------------------------------------------
    // Viewport / Scissor
    // ------------------------------------------------------------------------
    void VKCommandList::SetViewport(const Viewport& vp)
    {
        if (!m_cmd) return;
        VkViewport v{};
        v.x        = vp.x;
        v.y        = vp.y;
        v.width    = vp.width;
        v.height   = vp.height;
        v.minDepth = vp.minDepth;
        v.maxDepth = vp.maxDepth;
        vkCmdSetViewport(m_cmd, 0, 1, &v);
    }

    void VKCommandList::SetScissor(const Rect2D& sc)
    {
        if (!m_cmd) return;
        VkRect2D r{};
        r.offset = { sc.offsetX, sc.offsetY };
        r.extent = { sc.width,   sc.height };
        vkCmdSetScissor(m_cmd, 0, 1, &r);
    }

    // ------------------------------------------------------------------------
    // Pipeline / Buffer 绑定
    // ------------------------------------------------------------------------
    void VKCommandList::BindPipeline(PipelineHandle pipeline)
    {
        if (!m_cmd) return;
        const VKPipelineEntry* pe = m_device->LookupPipeline(pipeline);
        if (!pe)
        {
            LOG_STREAM_ERROR("VKCommandList") << "BindPipeline: invalid handle";
            return;
        }
        // 切换 pipeline 时清空 set 状态缓存与「当前已绑定」标记。
        // 不同 pipeline 的 setLayout 可能不兼容（descriptor 类型 / binding 数量
        // 不同），复用旧状态是 UB；即使切回同一 pipeline 也清空——保守但安全。
        // 帧内 DS 内容缓存按 layout 区分 key，可跨 pipeline 保留。
        if (pe->pipeline != m_currentPipeline)
        {
            m_boundSets.fill(VK_NULL_HANDLE);
            for (auto& s : m_setStateCache) s = TitusRHI::ResourceSetDesc{};
        }
        // 按 PipelineEntry 记录的 bindPoint 区分 Graphics / Compute
        vkCmdBindPipeline(m_cmd, pe->bindPoint, pe->pipeline);
        m_currentPipeline = pe->pipeline;
        m_currentLayout   = pe->layout;
        m_currentBindPoint = pe->bindPoint;
        m_currentPipelineEntry = pe;   // 供 BindResourceSet 查 setLayouts
    }

    void VKCommandList::BindVertexBuffer(uint32_t slot, BufferHandle buffer, uint64_t offset)
    {
        if (!m_cmd) return;
        const VKBufferEntry* be = m_device->LookupBuffer(buffer);
        if (!be) return;
        VkDeviceSize off = static_cast<VkDeviceSize>(offset);
        vkCmdBindVertexBuffers(m_cmd, slot, 1, &be->buffer, &off);
    }

    void VKCommandList::BindIndexBuffer(BufferHandle buffer, IndexType type, uint64_t offset)
    {
        if (!m_cmd) return;
        const VKBufferEntry* be = m_device->LookupBuffer(buffer);
        if (!be) return;
        vkCmdBindIndexBuffer(m_cmd, be->buffer,
                             static_cast<VkDeviceSize>(offset),
                             ToVkIndexType(type));
    }

    // ------------------------------------------------------------------------
    // BindResourceSet —— 帧内 DS 内容缓存
    //   1) 合并增量 binding 得到完整 ResourceSetDesc
    //   2) 按 (layout + 内容) 查本帧缓存：命中则复用 DS
    //   3) 未命中：Allocate + Update，写入缓存
    //   4) 若与当前已绑定 DS 不同，再 vkCmdBindDescriptorSets
    // ------------------------------------------------------------------------
    void VKCommandList::BindResourceSet(uint32_t setIndex, const ResourceSetDesc& setDesc)
    {
        if (!m_cmd || !m_currentPipelineEntry || !m_currentLayout) return;

        ++m_frameStats.bindResourceSetCalls;

        if (setIndex >= m_currentPipelineEntry->setLayouts.size())
        {
            LOG_STREAM_ERROR("VKCommandList") << "BindResourceSet: setIndex " << setIndex
                      << " out of range (pipeline has " << m_currentPipelineEntry->setLayouts.size()
                      << " sets)";
            return;
        }
        VkDescriptorSetLayout layout = m_currentPipelineEntry->setLayouts[setIndex];
        if (!layout)
        {
            LOG_STREAM_ERROR("VKCommandList") << "BindResourceSet: pipeline has no layout for set "
                      << setIndex;
            return;
        }

        // 合并新 binding 到 per-set 状态缓存（GL 风格增量绑定兼容）
        if (setIndex < kMaxCachedSets)
        {
            auto& cached = m_setStateCache[setIndex];
            for (const auto& nb : setDesc.bindings)
            {
                bool found = false;
                for (auto& cb : cached.bindings)
                {
                    if (cb.binding == nb.binding)
                    {
                        cb = nb;
                        found = true;
                        break;
                    }
                }
                if (!found) cached.bindings.push_back(nb);
            }
        }

        const TitusRHI::ResourceSetDesc& fullDesc =
            (setIndex < kMaxCachedSets) ? m_setStateCache[setIndex] : setDesc;

        const DsContentKey key = MakeContentKey(layout, fullDesc);
        VkDescriptorSet set = VK_NULL_HANDLE;

        auto it = m_dsContentCache.find(key);
        if (it != m_dsContentCache.end())
        {
            set = it->second;
            ++m_frameStats.descriptorCacheHits;
        }
        else
        {
            set = m_device->AllocateDescriptorSet(layout);
            if (!set) return;
            ++m_frameStats.descriptorAllocs;

            // 预先按 binding 数量预留容量，避免 vector reallocate 让指针失效。
            std::vector<VkDescriptorImageInfo>  imageInfos;
            std::vector<VkDescriptorBufferInfo> bufferInfos;
            imageInfos.reserve(fullDesc.bindings.size());
            bufferInfos.reserve(fullDesc.bindings.size());
#if defined(RENDERER_ENABLE_RAY_TRACING)
            std::vector<VkWriteDescriptorSetAccelerationStructureKHR> asWrites;
            std::vector<VkAccelerationStructureKHR>                   asHandles;
            asWrites.reserve(fullDesc.bindings.size());
            asHandles.reserve(fullDesc.bindings.size());
#endif

            std::vector<VkWriteDescriptorSet> writes;
            writes.reserve(fullDesc.bindings.size());

            for (const auto& b : fullDesc.bindings)
            {
                VkWriteDescriptorSet w{};
                w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet          = set;
                w.dstBinding      = b.binding;
                w.dstArrayElement = 0;
                w.descriptorCount = 1;
                w.descriptorType  = ToVkDescriptorType(b.type);

                switch (b.type)
                {
                case ResourceBindingType::UniformBuffer:
                case ResourceBindingType::StorageBuffer:
                    {
                        const VKBufferEntry* be = m_device->LookupBuffer(b.buffer);
                        if (!be) continue;
                        VkDescriptorBufferInfo bi{};
                        bi.buffer = be->buffer;
                        bi.offset = b.bufferOffset;
                        bi.range  = b.bufferRange ? b.bufferRange : VK_WHOLE_SIZE;
                        bufferInfos.push_back(bi);
                        w.pBufferInfo = &bufferInfos.back();
                        break;
                    }
                case ResourceBindingType::SampledTexture:
                case ResourceBindingType::CombinedImageSampler:
                case ResourceBindingType::StorageTexture:
                    {
                        const VKTextureEntry* te = m_device->LookupTexture(b.texture);
                        if (!te) continue;
                        VkDescriptorImageInfo ii{};
                        ii.imageView   = te->defaultView;
                        if (b.type == ResourceBindingType::StorageTexture)
                        {
                            ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                        }
                        else
                        {
                            ii.imageLayout = (te->currentLayout == VK_IMAGE_LAYOUT_GENERAL)
                                           ? VK_IMAGE_LAYOUT_GENERAL
                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        }
                        if (b.type == ResourceBindingType::CombinedImageSampler)
                        {
                            const VKSamplerEntry* se = m_device->LookupSampler(b.sampler);
                            if (se) ii.sampler = se->sampler;
                        }
                        imageInfos.push_back(ii);
                        w.pImageInfo = &imageInfos.back();
                        break;
                    }
                case ResourceBindingType::Sampler:
                    {
                        const VKSamplerEntry* se = m_device->LookupSampler(b.sampler);
                        if (!se) continue;
                        VkDescriptorImageInfo ii{};
                        ii.sampler = se->sampler;
                        imageInfos.push_back(ii);
                        w.pImageInfo = &imageInfos.back();
                        break;
                    }
#if defined(RENDERER_ENABLE_RAY_TRACING)
                case ResourceBindingType::AccelerationStructure:
                    {
                        const VKAccelStructEntry* ae = m_device->LookupAccelStruct(b.accelStruct);
                        if (!ae || !ae->as) continue;
                        asHandles.push_back(ae->as);
                        VkWriteDescriptorSetAccelerationStructureKHR asw{
                            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
                        asw.accelerationStructureCount = 1;
                        asw.pAccelerationStructures    = &asHandles.back();
                        asWrites.push_back(asw);
                        w.pNext = &asWrites.back();
                        break;
                    }
#endif
                default:
                    continue;
                }

                writes.push_back(w);
            }

            if (!writes.empty())
            {
                vkUpdateDescriptorSets(m_device->GetVkContext()->GetDevice(),
                                       static_cast<uint32_t>(writes.size()),
                                       writes.data(), 0, nullptr);
            }

            m_dsContentCache.emplace(key, set);
        }

        if (setIndex < kMaxCachedSets && m_boundSets[setIndex] == set)
            return;

        vkCmdBindDescriptorSets(m_cmd,
                                m_currentBindPoint,
                                m_currentLayout,
                                setIndex,
                                1, &set,
                                0, nullptr);
        if (setIndex < kMaxCachedSets)
            m_boundSets[setIndex] = set;
    }

    void VKCommandList::PushConstants(ShaderStage stages, uint32_t offset, uint32_t size, const void* data)
    {
        if (!m_cmd || !m_currentLayout || !data || size == 0) return;
        vkCmdPushConstants(m_cmd, m_currentLayout,
                           ToVkShaderStageFlags(stages),
                           offset, size, data);
    }

    // ------------------------------------------------------------------------
    // 绘制
    // ------------------------------------------------------------------------
    void VKCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance)
    {
        if (!m_cmd) return;
        vkCmdDraw(m_cmd, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void VKCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset,
                                    uint32_t firstInstance)
    {
        if (!m_cmd) return;
        vkCmdDrawIndexed(m_cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    // ------------------------------------------------------------------------
    // Compute
    //   - Dispatch 直接转发为 vkCmdDispatch
    //   - PipelineBarrier 采用保守翻译：src/dstStage 走不完全映射表，全局内存
    //     屏障只处理 storage write → shader read 这类常见依赖。textureBarriers
    //     需查 VKTextureEntry.currentLayout。
    // ------------------------------------------------------------------------
    void VKCommandList::Dispatch(uint32_t groupCountX,
                                 uint32_t groupCountY,
                                 uint32_t groupCountZ)
    {
        if (!m_cmd) return;
        if (m_currentBindPoint != VK_PIPELINE_BIND_POINT_COMPUTE)
        {
            LOG_STREAM_ERROR("VKCommandList") << "Dispatch: current pipeline is not a compute pipeline";
            return;
        }
        vkCmdDispatch(m_cmd, groupCountX, groupCountY, groupCountZ);
    }

#if defined(RENDERER_ENABLE_RAY_TRACING)
    // 光追派发：使用当前绑定 RT 管线的四个 SBT region。
    void VKCommandList::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
    {
        if (!m_cmd) return;
        if (m_currentBindPoint != VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
            || !m_currentPipelineEntry || !m_currentPipelineEntry->isRayTracing)
        {
            LOG_STREAM_ERROR("VKCommandList") << "TraceRays: current pipeline is not a ray tracing pipeline";
            return;
        }
        const auto& fn = m_device->GetVkContext()->RT();
        if (!fn.vkCmdTraceRaysKHR)
        {
            LOG_STREAM_ERROR("VKCommandList") << "TraceRays: vkCmdTraceRaysKHR unavailable";
            return;
        }
        fn.vkCmdTraceRaysKHR(m_cmd,
                             &m_currentPipelineEntry->raygenRegion,
                             &m_currentPipelineEntry->missRegion,
                             &m_currentPipelineEntry->hitRegion,
                             &m_currentPipelineEntry->callableRegion,
                             width, height, depth);
    }

    // 加速结构构建/refit：
    //   命令流内更新 TLAS。要求 target 为创建时带 AllowUpdate 的 TLAS，且本次
    //   instance 数量不超过创建容量。instance 数量与创建时相同且 info.update 为
    //   true 时走 UPDATE（refit）模式，否则在原 AS 上就地 BUILD（重建）。
    //   调用方需在本命令之后用 PipelineBarrier 表达「AS 写 → shader 读」依赖。
    void VKCommandList::BuildAccelerationStructure(AccelerationStructureHandle target,
                                                   const AccelerationStructureBuildInfo& info)
    {
        if (!m_cmd) return;
        const VKAccelStructEntry* e = m_device->LookupAccelStruct(target);
        if (!e || e->type != AccelerationStructureType::TopLevel)
        {
            LOG_STREAM_ERROR("VKCommandList") << "BuildAccelerationStructure: target is not TLAS or is invalid";
            return;
        }
        if (!e->allowUpdate || !e->updateScratch)
        {
            LOG_STREAM_ERROR("VKCommandList") << "BuildAccelerationStructure: TLAS was not created with AllowUpdate";
            return;
        }
        const uint32_t instCount = static_cast<uint32_t>(info.instances.size());
        if (instCount == 0 || instCount > e->instanceCapacity)
        {
            LOG_STREAM_ERROR("VKCommandList") << "BuildAccelerationStructure: invalid instance count ("
                << instCount << " / cap " << e->instanceCapacity << ")";
            return;
        }
        const auto& fn = m_device->GetVkContext()->RT();

        // 1) 重新打包 instance 到 host-visible instance buffer
        std::vector<VkAccelerationStructureInstanceKHR> vkInst;
        vkInst.reserve(instCount);
        for (const auto& inst : info.instances)
        {
            const VKAccelStructEntry* blas = m_device->LookupAccelStruct(inst.blas);
            if (!blas) { LOG_STREAM_ERROR("VKCommandList") << "refit: invalid BLAS"; return; }
            VkAccelerationStructureInstanceKHR vi{};
            std::memcpy(&vi.transform, inst.transform, sizeof(float) * 12);
            vi.instanceCustomIndex                    = inst.instanceCustomIndex & 0xFFFFFF;
            vi.mask                                   = inst.mask & 0xFF;
            vi.instanceShaderBindingTableRecordOffset = inst.shaderBindingTableOffset & 0xFFFFFF;
            vi.flags                                  = inst.flags & 0xFF;
            vi.accelerationStructureReference         = blas->deviceAddress;
            vkInst.push_back(vi);
        }
        const VkDeviceSize bytes = sizeof(VkAccelerationStructureInstanceKHR) * vkInst.size();
        void* mapped = nullptr;
        if (vkMapMemory(m_device->GetVkContext()->GetDevice(), e->instanceMemory, 0, bytes, 0, &mapped) != VK_SUCCESS)
            return;
        std::memcpy(mapped, vkInst.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(m_device->GetVkContext()->GetDevice(), e->instanceMemory);

        // 2) 组装 geometry（instances）
        VkAccelerationStructureGeometryKHR geo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geo.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geo.geometry.instances.arrayOfPointers = VK_FALSE;
        geo.geometry.instances.data.deviceAddress = e->instanceBufferAddr;

        // 3) build/update：仅当请求 update 且数量与创建时一致时走 refit
        const bool doUpdate = info.update && (instCount == e->instanceCapacity);
        VkAccelerationStructureBuildGeometryInfoKHR bi{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        bi.type  = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        bi.flags = e->buildFlags;
        bi.mode  = doUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                            : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        bi.srcAccelerationStructure = doUpdate ? e->as : VK_NULL_HANDLE;
        bi.dstAccelerationStructure = e->as;
        bi.geometryCount = 1;
        bi.pGeometries   = &geo;
        bi.scratchData.deviceAddress = e->updateScratchAddr;

        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = instCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;

        fn.vkCmdBuildAccelerationStructuresKHR(m_cmd, 1, &bi, &pRange);
    }
#endif

    namespace
    {
        VkPipelineStageFlags ToVkPipelineStageFlags(TitusRHI::PipelineStage s)
        {
            using TitusRHI::PipelineStage;
            VkPipelineStageFlags f = 0;
            const uint32_t v = static_cast<uint32_t>(s);
            if (v & static_cast<uint32_t>(PipelineStage::VertexInput))     f |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            if (v & static_cast<uint32_t>(PipelineStage::VertexShader))    f |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
            if (v & static_cast<uint32_t>(PipelineStage::FragmentShader))  f |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            if (v & static_cast<uint32_t>(PipelineStage::ComputeShader))   f |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            if (v & static_cast<uint32_t>(PipelineStage::ColorAttachment)) f |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            if (v & static_cast<uint32_t>(PipelineStage::DepthAttachment)) f |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                                                              | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            if (v & static_cast<uint32_t>(PipelineStage::Transfer))        f |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            if (v & static_cast<uint32_t>(PipelineStage::AllGraphics))     f |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
            if (v & static_cast<uint32_t>(PipelineStage::AllCommands))     f |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
#if defined(RENDERER_ENABLE_RAY_TRACING)
            // 光追：AS 构建阶段
            if (v & static_cast<uint32_t>(PipelineStage::AccelerationStructureBuild))
                f |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
#endif
            // 退化：未指定时默认 ALL_COMMANDS，避免 Validation Layer 报错
            if (f == 0) f = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            return f;
        }

        VkAccessFlags ToVkAccessFlags(TitusRHI::AccessFlags a)
        {
            using TitusRHI::AccessFlags;
            VkAccessFlags f = 0;
            const uint32_t v = static_cast<uint32_t>(a);
            if (v & static_cast<uint32_t>(AccessFlags::ShaderRead))           f |= VK_ACCESS_SHADER_READ_BIT;
            if (v & static_cast<uint32_t>(AccessFlags::ShaderWrite))          f |= VK_ACCESS_SHADER_WRITE_BIT;
            if (v & static_cast<uint32_t>(AccessFlags::ColorAttachmentRead))  f |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            if (v & static_cast<uint32_t>(AccessFlags::ColorAttachmentWrite)) f |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            if (v & static_cast<uint32_t>(AccessFlags::DepthRead))            f |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            if (v & static_cast<uint32_t>(AccessFlags::DepthWrite))           f |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            if (v & static_cast<uint32_t>(AccessFlags::TransferRead))         f |= VK_ACCESS_TRANSFER_READ_BIT;
            if (v & static_cast<uint32_t>(AccessFlags::TransferWrite))        f |= VK_ACCESS_TRANSFER_WRITE_BIT;
#if defined(RENDERER_ENABLE_RAY_TRACING)
            // 光追：AS 读写访问
            if (v & static_cast<uint32_t>(AccessFlags::AccelerationStructureRead))
                f |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            if (v & static_cast<uint32_t>(AccessFlags::AccelerationStructureWrite))
                f |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
#endif
            return f;
        }
    }

    namespace
    {
        VkImageLayout ToVkImageLayout(TitusRHI::TextureLayout l)
        {
            using TitusRHI::TextureLayout;
            switch (l)
            {
            case TextureLayout::Undefined:              return VK_IMAGE_LAYOUT_UNDEFINED;
            case TextureLayout::General:                return VK_IMAGE_LAYOUT_GENERAL;
            case TextureLayout::ShaderReadOnly:         return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case TextureLayout::ColorAttachment:        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case TextureLayout::DepthStencilAttachment: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case TextureLayout::TransferSrc:            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            case TextureLayout::TransferDst:            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case TextureLayout::PresentSrc:             return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            }
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    void VKCommandList::PipelineBarrier(const TitusRHI::PipelineBarrierDesc& desc)
    {
        if (!m_cmd) return;

        const VkPipelineStageFlags srcStage = ToVkPipelineStageFlags(desc.srcStage);
        const VkPipelineStageFlags dstStage = ToVkPipelineStageFlags(desc.dstStage);

        // 全局内存屏障：对应 desc.srcGlobalAccess / dstGlobalAccess
        VkMemoryBarrier mb{};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = ToVkAccessFlags(desc.srcGlobalAccess);
        mb.dstAccessMask = ToVkAccessFlags(desc.dstGlobalAccess);

        // textureBarriers —— layout 转换（含 access scope）
        std::vector<VkImageMemoryBarrier> imageBars;
        imageBars.reserve(desc.textureBarriers.size());
        for (const auto& tb : desc.textureBarriers)
        {
            VKTextureEntry* te = m_device->MutableLookupTexture(tb.texture);
            if (!te) continue;

            VkImageMemoryBarrier ib{};
            ib.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ib.oldLayout                       = (tb.oldLayout == TitusRHI::TextureLayout::Undefined)
                                                 ? te->currentLayout
                                                 : ToVkImageLayout(tb.oldLayout);
            ib.newLayout                       = ToVkImageLayout(tb.newLayout);
            ib.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            ib.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            ib.image                           = te->image;
            // 简化：根据已知 format 推断 aspect（depth/stencil 留作后续扩展）
            ib.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            ib.subresourceRange.baseMipLevel   = 0;
            ib.subresourceRange.levelCount     = te->mipLevels  ? te->mipLevels  : 1;
            ib.subresourceRange.baseArrayLayer = 0;
            ib.subresourceRange.layerCount     = te->arrayLayers ? te->arrayLayers : 1;
            ib.srcAccessMask                   = ToVkAccessFlags(tb.srcAccess);
            ib.dstAccessMask                   = ToVkAccessFlags(tb.dstAccess);

            imageBars.push_back(ib);
            // 更新跟踪状态
            te->currentLayout = ib.newLayout;
        }

        vkCmdPipelineBarrier(m_cmd, srcStage, dstStage,
                             /*dependencyFlags*/ 0,
                             /*memoryBarrierCount*/ 1, &mb,
                             0, nullptr,
                             static_cast<uint32_t>(imageBars.size()),
                             imageBars.empty() ? nullptr : imageBars.data());
    }
}
