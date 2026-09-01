// ============================================================================
// Renderer (OpenGL) - GLCommandList.cpp
// 录制阶段把后端无关命令转换为 std::function 加入延迟队列；Submit 时回放。
// 该机制兼容现有 RenderCommandBuffer 的设计哲学。
// ============================================================================
#include "GLCommandList.h"
#include "GLDevice.h"
#include "GLTranslate.h"

#include <iostream>
#include <cstring>
#include <cstdio>
#include "Logger.h"

namespace TitusGraphics
{
    using namespace TitusRHI;

    GLCommandList::GLCommandList(GLDevice* device)
        : m_device(device)
    {
    }

    void GLCommandList::Reset()
    {
        m_commands.clear();
        m_currentTopology = PrimitiveTopology::TriangleList;
        m_currentIndexType = IndexType::UInt32;
        m_currentIndexOffset = 0;
        m_currentVAO = 0;
        m_currentProgram = 0;
        m_currentIsCompute = false;
        m_currentPushRanges.clear();
        m_currentVertexBindings.clear();
    }

    void GLCommandList::Replay()
    {
        for (auto& fn : m_commands) fn();
        m_commands.clear();
    }

    void GLCommandList::Enqueue(std::function<void()> cmd)
    {
        m_commands.emplace_back(std::move(cmd));
    }

    // ------------------------------------------------------------------------
    // RenderPass 模拟：BindFBO + Clear（按 LoadOp）+ Invalidate（按 StoreOp）
    // ------------------------------------------------------------------------
    void GLCommandList::BeginRenderPass(const RenderPassBeginInfo& info)
    {
        // 拷贝结构体捕获，避免悬空引用
        const RenderPassBeginInfo cap = info;
        GLDevice* dev = m_device;
        Enqueue([cap, dev]()
        {
            GLuint fbo = 0;
            uint32_t rtW = dev->GetDefaultWidth();
            uint32_t rtH = dev->GetDefaultHeight();
            if (cap.renderTarget.IsValid())
            {
                if (const GLRenderTargetEntry* rt = dev->LookupRenderTarget(cap.renderTarget))
                {
                    fbo = rt->fbo;
                    rtW = rt->width;
                    rtH = rt->height;
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);

            // 视口（默认按 RT 全尺寸）
            const uint32_t areaW = cap.renderArea.width ? cap.renderArea.width : rtW;
            const uint32_t areaH = cap.renderArea.height ? cap.renderArea.height : rtH;
            glViewport(cap.renderArea.offsetX, cap.renderArea.offsetY, static_cast<GLsizei>(areaW), static_cast<GLsizei>(areaH));

            // ⭐ 关键：glClear 受 SCISSOR_TEST / glColorMask / glDepthMask 影响。
            //   上一帧/上一 Pass 可能开过 SCISSOR_TEST 或关过写掩码，会让本次
            //   clear 只清屏幕左下角某块。RenderPass 语义要求"整 RT 范围被清"，
            //   因此先把这三类状态恢复到全开/全清，再调用 glClear；之后由
            //   后续 SetScissor / BindPipeline 各自重新设置即可。
            glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
            glStencilMask(0xFF);

            // LoadOp::Clear → glClear；其他 LoadOp 不触发清屏
            GLbitfield clearMask = 0;
            if (!cap.colorOps.empty() && cap.colorOps[0].loadOp == LoadOp::Clear)
            {
                const auto& c = cap.colorOps[0].clearValue.color;
                glClearColor(c[0], c[1], c[2], c[3]);
                clearMask |= GL_COLOR_BUFFER_BIT;
            }
            if (cap.hasDepthStencil && cap.depthStencilOp.loadOp == LoadOp::Clear)
            {
                glClearDepth(static_cast<double>(cap.depthStencilOp.clearValue.depth));
                glClearStencil(static_cast<GLint>(cap.depthStencilOp.clearValue.stencil));
                clearMask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
            }
            if (clearMask) glClear(clearMask);
        });
    }

    void GLCommandList::EndRenderPass()
    {
        // StoreOp::DontCare → glInvalidateFramebuffer；其他保留默认存储
        // 这里只处理默认 backbuffer 的最常见情况，自定义 FBO 的精细化将在后续阶段补齐。
        Enqueue([]()
        {
            // 解绑 FBO 回到默认（可选；多数情况下下一个 Pass 会重新 BindFramebuffer）
            // 此处保留默认存储语义，避免误丢弃后缓冲。
        });
    }

    // ------------------------------------------------------------------------
    // Viewport / Scissor
    // ------------------------------------------------------------------------
    void GLCommandList::SetViewport(const Viewport& vp)
    {
        const Viewport cap = vp;
        Enqueue([cap]()
        {
            glViewport(static_cast<GLint>(cap.x),
                       static_cast<GLint>(cap.y),
                       static_cast<GLsizei>(cap.width),
                       static_cast<GLsizei>(cap.height));
            glDepthRange(static_cast<double>(cap.minDepth), static_cast<double>(cap.maxDepth));
        });
    }

    void GLCommandList::SetScissor(const Rect2D& sc)
    {
        const Rect2D cap = sc;
        Enqueue([cap]()
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(cap.offsetX, cap.offsetY,
                      static_cast<GLsizei>(cap.width),
                      static_cast<GLsizei>(cap.height));
        });
    }

    // ------------------------------------------------------------------------
    // 资源绑定
    // ------------------------------------------------------------------------
    void GLCommandList::BindPipeline(PipelineHandle pipeline)
    {
        const GLPipelineEntry* pe = m_device->LookupPipeline(pipeline);
        if (!pe)
        {
            LOG_STREAM_ERROR("GLCommandList") << "BindPipeline: invalid handle";
            return;
        }
        // 录制阶段记录 program/compute 状态以供后续 Draw / Dispatch / PushConstants 使用
        m_currentTopology = pe->topology;
        m_currentVAO = pe->vao;
        m_currentProgram = pe->program;
        m_currentIsCompute = pe->isCompute;
        m_currentPushRanges = pe->pushConstantRanges;
        m_currentVertexBindings = pe->vertexLayout.bindings;

        const GLuint program = pe->program;
        const GLuint vao = pe->vao;
        const bool isCompute = pe->isCompute;
        const RasterizerState rs = pe->rasterizer;
        const DepthStencilState ds = pe->depthStencil;
        const BlendState bs = pe->blend;

        Enqueue([program, vao, isCompute, rs, ds, bs]()
        {
            glUseProgram(program);
            if (isCompute)
            {
                // 计算管线：不需要设置光栅化/深度/混合/VAO，避免在后续 graphics Pass 中污染状态。
                return;
            }
            if (vao)
                glBindVertexArray(vao);

            // 光栅化
            if (rs.cullMode == CullMode::None)
            {
                glDisable(GL_CULL_FACE);
            }
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(ToGLCullMode(rs.cullMode));
            }
            glFrontFace(ToGLFrontFace(rs.frontFace));
            glPolygonMode(GL_FRONT_AND_BACK, ToGLPolygonMode(rs.polygonMode));
            glEnable(GL_MULTISAMPLE);

            // 深度
            if (ds.depthTestEnable) glEnable(GL_DEPTH_TEST);
            else glDisable(GL_DEPTH_TEST);
            glDepthMask(ds.depthWriteEnable ? GL_TRUE : GL_FALSE);
            glDepthFunc(ToGLCompareOp(ds.depthCompareOp));

            // 混合（仅处理第 0 个 RT）
            if (!bs.attachments.empty())
            {
                const auto& a = bs.attachments[0];
                if (a.blendEnable) glEnable(GL_BLEND);
                else glDisable(GL_BLEND);
                glBlendFuncSeparate(ToGLBlendFactor(a.srcColorBlendFactor),
                                    ToGLBlendFactor(a.dstColorBlendFactor),
                                    ToGLBlendFactor(a.srcAlphaBlendFactor),
                                    ToGLBlendFactor(a.dstAlphaBlendFactor));
                glBlendEquationSeparate(ToGLBlendOp(a.colorBlendOp),
                                        ToGLBlendOp(a.alphaBlendOp));
                glColorMask((a.colorWriteMask & 0x1) ? GL_TRUE : GL_FALSE,
                            (a.colorWriteMask & 0x2) ? GL_TRUE : GL_FALSE,
                            (a.colorWriteMask & 0x4) ? GL_TRUE : GL_FALSE,
                            (a.colorWriteMask & 0x8) ? GL_TRUE : GL_FALSE);
            }
            else
            {
                glDisable(GL_BLEND);
            }
        });
    }

    void GLCommandList::BindVertexBuffer(uint32_t slot, BufferHandle buffer, uint64_t offset)
    {
        const GLBufferEntry* be = m_device->LookupBuffer(buffer);
        if (!be) return;
        const GLuint bufId = be->id;
        const GLuint vao = m_currentVAO;

        // 从当前 Pipeline 的 vertex bindings 中查出 stride（与 GLDevice 创建管线
        // 时调用的 glVertexAttribFormat / glVertexAttribBinding 使用同一套 binding。
        // 没查到则退化使用 0（后端会报 GL_INVALID_VALUE，能被发现）。
        GLsizei stride = 0;
        for (const auto& vb : m_currentVertexBindings)
        {
            if (vb.binding == slot)
            {
                stride = static_cast<GLsizei>(vb.stride);
                break;
            }
        }

        Enqueue([slot, bufId, offset, stride, vao]()
        {
            if (vao)
                glBindVertexArray(vao);
            // 使用 DSA-风格的 glBindVertexBuffer：把 VBO 绑到 VAO 的
            //   binding=slot 槽位，提供 stride。Pipeline 创建时 已调
            //   glVertexAttribFormat + glVertexAttribBinding 让 attribute 指
            //   向 binding=slot，这里补上 VBO 与 stride 后 attribute 才能被
            //   正确取出；glBindBuffer(GL_ARRAY_BUFFER) 仅在 “compat
            //   glVertexAttribPointer” 路径下有效，与 DSA 不兼容。
            glBindVertexBuffer(slot,
                               bufId,
                               static_cast<GLintptr>(offset),
                               stride);
        });
    }

    void GLCommandList::BindIndexBuffer(BufferHandle buffer, IndexType type, uint64_t offset)
    {
        const GLBufferEntry* be = m_device->LookupBuffer(buffer);
        if (!be) return;
        const GLuint bufId = be->id;
        m_currentIndexType = type;
        m_currentIndexOffset = offset;
        Enqueue([bufId]()
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufId);
        });
    }

    void GLCommandList::BindResourceSet(uint32_t /*setIndex*/, const ResourceSetDesc& setDesc)
    {
        // OpenGL 没有 descriptor set 概念，按 binding 直接走 glBindBufferRange / glBindTextureUnit。
        // 这里把 setDesc 拷贝到 lambda 内回放。
        const ResourceSetDesc cap = setDesc;
        GLDevice* dev = m_device;
        Enqueue([cap, dev]()
        {
            for (const auto& b : cap.bindings)
            {
                switch (b.type)
                {
                case ResourceBindingType::UniformBuffer:
                    {
                        if (const GLBufferEntry* be = dev->LookupBuffer(b.buffer))
                        {
                            glBindBufferRange(GL_UNIFORM_BUFFER, b.binding, be->id,
                                              static_cast<GLintptr>(b.bufferOffset),
                                              static_cast<GLsizeiptr>(b.bufferRange ? b.bufferRange : be->size));
                        }
                        break;
                    }
                case ResourceBindingType::StorageBuffer:
                    {
                        if (const GLBufferEntry* be = dev->LookupBuffer(b.buffer))
                        {
                            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, b.binding, be->id,
                                              static_cast<GLintptr>(b.bufferOffset),
                                              static_cast<GLsizeiptr>(b.bufferRange ? b.bufferRange : be->size));
                        }
                        break;
                    }
                case ResourceBindingType::SampledTexture:
                case ResourceBindingType::CombinedImageSampler:
                    {
                        if (const GLTextureEntry* te = dev->LookupTexture(b.texture))
                        {
                            glActiveTexture(GL_TEXTURE0 + b.binding);
                            glBindTexture(te->target, te->id);
                        }
                        if (b.type == ResourceBindingType::CombinedImageSampler)
                        {
                            if (const GLSamplerEntry* se = dev->LookupSampler(b.sampler))
                                glBindSampler(b.binding, se->id);
                        }
                        break;
                    }
                case ResourceBindingType::Sampler:
                    {
                        if (const GLSamplerEntry* se = dev->LookupSampler(b.sampler))
                            glBindSampler(b.binding, se->id);
                        break;
                    }
                case ResourceBindingType::StorageTexture:
                    {
                        if (const GLTextureEntry* te = dev->LookupTexture(b.texture))
                            glBindImageTexture(b.binding, te->id, 0, GL_FALSE, 0, GL_READ_WRITE, te->internalFmt);
                        break;
                    }
                }
            }
        });
    }

    void GLCommandList::PushConstants(ShaderStage /*stages*/, uint32_t offset,
                                      uint32_t size, const void* data)
    {
        // OpenGL 没有 push constants；后端将其语义映射到 program 中的 uniform。
        // 落地策略：
        //   - 优先：在 BindPipeline 时记下了 m_currentPushRanges；如果 (offset,size) 与
        //     某 range 匹配且其 glName 非空，则直接查该 uniform；
        //   - 兜底：按 "PC_<offset>" 查（约定式）。
        if (!data || size == 0) return;
        std::vector<uint8_t> bytes(static_cast<const uint8_t*>(data),
                                   static_cast<const uint8_t*>(data) + size);
        const uint32_t off = offset;
        const GLuint program = m_currentProgram;
        // 拷贝匹配到的 glName（字符串）以避免悬空引用
        std::string explicitName;
        for (const auto& r : m_currentPushRanges)
        {
            if (r.offset == off && r.size == size)
            {
                explicitName = r.glName;
                break;
            }
        }
        Enqueue([bytes, off, size, program, explicitName]()
        {
            if (program == 0) return;
            GLint loc = -1;
            std::string nameStr = explicitName;
            if (!nameStr.empty())
            {
                loc = glGetUniformLocation(program, nameStr.c_str());
            }
            if (loc < 0)
            {
                char nameBuf[32];
                std::snprintf(nameBuf, sizeof(nameBuf), "PC_%u", off);
                nameStr = nameBuf;
                loc = glGetUniformLocation(program, nameStr.c_str());
            }
            if (loc < 0) return;

            // 查询 uniform 的实际类型，按类型选择正确的 glUniform* 调用。
            // 关键：对 int / uint uniform 用 glUniform*fv 会触发 GL_INVALID_OPERATION
            // 且 uniform 值不会被更新（GL 规范）。
            GLenum gltype = 0;
            GLint  glcount = 0;
            {
                // 找到 active uniform index 才能查类型；用名字匹配。
                const GLchar* names[1] = { nameStr.c_str() };
                GLuint uniformIndex = GL_INVALID_INDEX;
                glGetUniformIndices(program, 1, names, &uniformIndex);
                if (uniformIndex != GL_INVALID_INDEX)
                {
                    glGetActiveUniformsiv(program, 1, &uniformIndex, GL_UNIFORM_TYPE, reinterpret_cast<GLint*>(&gltype));
                    glGetActiveUniformsiv(program, 1, &uniformIndex, GL_UNIFORM_SIZE, &glcount);
                }
            }

            const float* fv = reinterpret_cast<const float*>(bytes.data());
            const GLint* iv = reinterpret_cast<const GLint*>(bytes.data());
            const GLuint* uv = reinterpret_cast<const GLuint*>(bytes.data());

            // 按 GL uniform 类型派发；查询失败时回退到按 size 推断 float 类型。
            switch (gltype)
            {
            case GL_FLOAT:           glUniform1fv(loc, 1, fv);                          break;
            case GL_FLOAT_VEC2:      glUniform2fv(loc, 1, fv);                          break;
            case GL_FLOAT_VEC3:      glUniform3fv(loc, 1, fv);                          break;
            case GL_FLOAT_VEC4:      glUniform4fv(loc, 1, fv);                          break;
            case GL_INT:             glUniform1iv(loc, 1, iv);                          break;
            case GL_INT_VEC2:        glUniform2iv(loc, 1, iv);                          break;
            case GL_INT_VEC3:        glUniform3iv(loc, 1, iv);                          break;
            case GL_INT_VEC4:        glUniform4iv(loc, 1, iv);                          break;
            case GL_UNSIGNED_INT:    glUniform1uiv(loc, 1, uv);                         break;
            case GL_UNSIGNED_INT_VEC2: glUniform2uiv(loc, 1, uv);                       break;
            case GL_UNSIGNED_INT_VEC3: glUniform3uiv(loc, 1, uv);                       break;
            case GL_UNSIGNED_INT_VEC4: glUniform4uiv(loc, 1, uv);                       break;
            case GL_FLOAT_MAT2:      glUniformMatrix2fv(loc, 1, GL_FALSE, fv);          break;
            case GL_FLOAT_MAT3:      glUniformMatrix3fv(loc, 1, GL_FALSE, fv);          break;
            case GL_FLOAT_MAT4:      glUniformMatrix4fv(loc, 1, GL_FALSE, fv);          break;
            default:
                // Fallback：按 size 走 float 路径（兼容旧路径）。
                switch (size)
                {
                case sizeof(float) * 16: glUniformMatrix4fv(loc, 1, GL_FALSE, fv); break;
                case sizeof(float) * 9:  glUniformMatrix3fv(loc, 1, GL_FALSE, fv); break;
                case sizeof(float) * 4:  glUniform4fv(loc, 1, fv);                 break;
                case sizeof(float) * 3:  glUniform3fv(loc, 1, fv);                 break;
                case sizeof(float) * 2:  glUniform2fv(loc, 1, fv);                 break;
                case sizeof(float):      glUniform1fv(loc, 1, fv);                 break;
                default: break;
                }
                break;
            }
        });
    }

    // ------------------------------------------------------------------------
    // 绘制
    // ------------------------------------------------------------------------
    void GLCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance)
    {
        const GLenum prim = ToGLPrimitive(m_currentTopology);
        Enqueue([prim, vertexCount, instanceCount, firstVertex, firstInstance]()
        {
            if (instanceCount <= 1 && firstInstance == 0)
                glDrawArrays(prim, static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount));
            else
                glDrawArraysInstancedBaseInstance(prim,
                                                  static_cast<GLint>(firstVertex),
                                                  static_cast<GLsizei>(vertexCount),
                                                  static_cast<GLsizei>(instanceCount),
                                                  firstInstance);
        });
    }

    void GLCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset,
                                    uint32_t firstInstance)
    {
        const GLenum prim = ToGLPrimitive(m_currentTopology);
        const GLenum idxType = ToGLIndexType(m_currentIndexType);
        const GLsizei stride = IndexTypeBytes(m_currentIndexType);
        const uint64_t baseOff = m_currentIndexOffset;

        Enqueue([=]()
        {
            const void* offsetPtr = reinterpret_cast<const void*>(
                baseOff + static_cast<uint64_t>(firstIndex) * static_cast<uint64_t>(stride));
            if (instanceCount <= 1 && firstInstance == 0 && vertexOffset == 0)
            {
                glDrawElements(prim, static_cast<GLsizei>(indexCount), idxType, offsetPtr);
            }
            else
            {
                glDrawElementsInstancedBaseVertexBaseInstance(
                    prim,
                    static_cast<GLsizei>(indexCount),
                    idxType,
                    const_cast<void*>(offsetPtr),
                    static_cast<GLsizei>(instanceCount),
                    vertexOffset,
                    firstInstance);
            }
        });
    }

    // ------------------------------------------------------------------------
    // 计算 + 屏障
    // ------------------------------------------------------------------------
    void GLCommandList::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz)
    {
        Enqueue([gx, gy, gz]()
        {
            glDispatchCompute(gx, gy, gz);
        });
    }

    void GLCommandList::PipelineBarrier(const TitusRHI::PipelineBarrierDesc& desc)
    {
        // 把后端无关的 stage / access flags 翻译为 glMemoryBarrier 位掊码。
        // 默认策略：任何“ShaderWrite → ShaderRead”都使用 image / texture / SSBO 三者组合。
        GLbitfield bits = 0;
        if (HasFlag(desc.dstGlobalAccess, AccessFlags::ShaderRead)
            || HasFlag(desc.dstStage, PipelineStage::FragmentShader)
            || HasFlag(desc.dstStage, PipelineStage::VertexShader))
        {
            bits |= GL_TEXTURE_FETCH_BARRIER_BIT;
            bits |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
            bits |= GL_SHADER_STORAGE_BARRIER_BIT;
            bits |= GL_UNIFORM_BARRIER_BIT;
        }
        if (bits == 0)
        {
            // 保守 fallback：仅 image 访问屏障
            bits = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
        }
        Enqueue([bits]()
        {
            glMemoryBarrier(bits);
        });
    }

    void GLCommandList::ResolveTexture(TextureHandle src, TextureHandle dst)
    {
        GLDevice* dev = m_device;
        Enqueue([dev, src, dst]()
        {
            const GLTextureEntry* srcTex = dev->LookupTexture(src);
            const GLTextureEntry* dstTex = dev->LookupTexture(dst);
            if (!srcTex || !dstTex || srcTex->id == 0 || dstTex->id == 0)
            {
                LOG_STREAM_ERROR("GLCommandList") << "ResolveTexture: invalid src/dst";
                return;
            }

            GLuint fbos[2] = {0, 0};
            glGenFramebuffers(2, fbos);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   srcTex->target, srcTex->id, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   dstTex->target, dstTex->id, 0);

            const GLint w = static_cast<GLint>(srcTex->width);
            const GLint h = static_cast<GLint>(srcTex->height);
            glBlitFramebuffer(0, 0, w, h, 0, 0,
                              static_cast<GLint>(dstTex->width),
                              static_cast<GLint>(dstTex->height),
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(2, fbos);
        });
    }
}
