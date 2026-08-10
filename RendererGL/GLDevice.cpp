// ============================================================================
// Renderer (OpenGL) - GLDevice.cpp
// 继承 GThreadableDevice 后的子类实现：
//   - 基类负责参数校验、句柄分配、模板方法骨架（Init/Shutdown/CreateXxx）
//   - 本文件只实现 OnInitBackend / OnInitSwapchain / *Impl() 钩子
//   - WaitIdle 退化为 glFinish；BeginFrame/Present 在 GL 后端几乎为空
// ============================================================================
#include "GLDevice.h"
#include "GLCommandList.h"
#include "GLTranslate.h"

#include "RendererCore/IWindow.h"

#include <iostream>
#include "Logger.h"
#include <cstring>
#include <vector>
#include <algorithm>

namespace TitusGraphics
{
    using namespace TitusRHI;

    // ------------------------------------------------------------------------
    // 工具：根据 BufferUsage / MemoryUsage 选 GL target + usage hint
    // ------------------------------------------------------------------------
    static GLenum PickGLBufferTarget(BufferUsage usage)
    {
        if (HasFlag(usage, BufferUsage::IndexBuffer)) return GL_ELEMENT_ARRAY_BUFFER;
        if (HasFlag(usage, BufferUsage::UniformBuffer)) return GL_UNIFORM_BUFFER;
        if (HasFlag(usage, BufferUsage::StorageBuffer)) return GL_SHADER_STORAGE_BUFFER;
        if (HasFlag(usage, BufferUsage::Indirect)) return GL_DRAW_INDIRECT_BUFFER;
        return GL_ARRAY_BUFFER;
    }

    static GLenum PickGLBufferUsageHint(MemoryUsage mem)
    {
        switch (mem)
        {
        case MemoryUsage::GpuOnly: return GL_STATIC_DRAW;
        case MemoryUsage::CpuToGpu: return GL_DYNAMIC_DRAW;
        case MemoryUsage::GpuToCpu: return GL_DYNAMIC_READ;
        case MemoryUsage::CpuOnly: return GL_STREAM_DRAW;
        }
        return GL_STATIC_DRAW;
    }

    // ------------------------------------------------------------------------
    // 构造 / 析构
    // ------------------------------------------------------------------------
    GLDevice::GLDevice() = default;

    GLDevice::~GLDevice()
    {
        // 由基类 Shutdown 处理资源释放；析构期再保险一次
        if (m_initialized) Shutdown();
    }

    // ------------------------------------------------------------------------
    // 后端创建：假定 IWindow 已 makeContextCurrent；本方法只 glewInit + 查 Caps
    // ------------------------------------------------------------------------
    bool GLDevice::OnInitBackend(const GDeviceDesc& /*desc*/, IWindow* window)
    {
        if (window == nullptr)
        {
            LOG_STREAM_ERROR("GLDevice") << "OnInitBackend: window is null";
            return false;
        }

        // GLEW 初始化：上层 GLFWWindow 已经 makeContextCurrent，
        // 这里再次保险性 glewInit（重复调用是安全的）。
        glewExperimental = GL_TRUE;
        const GLenum r = glewInit();
        if (r != GLEW_OK)
        {
            LOG_STREAM_ERROR("GLDevice") << "glewInit failed: " << r;
            return false;
        }

        // 与 Vulkan 的 NDC 对齐：clip-space z ∈ [0, 1]（默认是 [-1, +1]）。
        // 配合全工程预编译宏 GLM_FORCE_DEPTH_ZERO_TO_ONE，让 TitusMath::ortho/perspective
        // （内部仍走 glm）
        // 在 GL/VK 两端输出完全相同的投影矩阵。要求 GL 4.5 / ARB_clip_control，
        // 当前 GLFW 上下文 hint 已请求 GL 4.5+（见 Platform/GLFWWindow.cpp 与
        // Renderer/GLFWWindow.cpp），驱动支持时 GLEW 会暴露 glClipControl 函数指针。
        if (GLEW_VERSION_4_5 || GLEW_ARB_clip_control)
        {
            glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
        }
        else
        {
            LOG_STREAM_WARN("GLDevice") << "glClipControl not supported; depth-range will mismatch VK";
        }

        // 方案 A：开启默认 framebuffer 的 sRGB 自动编码，与 VK 的 VK_FORMAT_B8G8R8A8_SRGB
        // swapchain 行为对齐。要求 GLFW 端已设置 GLFW_SRGB_CAPABLE=TRUE。
        // 启用后 FS 输出按 linear 颜色处理，由驱动统一做 linear→sRGB 编码写入 backbuffer，
        // 避免 ScreenQuad_FS 内的手动 pow(c, 1/2.2) 与硬件 sRGB 叠加导致的"VK 偏亮"。
        if (GLEW_VERSION_3_0 || GLEW_ARB_framebuffer_sRGB || GLEW_EXT_framebuffer_sRGB)
        {
            glEnable(GL_FRAMEBUFFER_SRGB);
        }
        else
        {
            LOG_STREAM_WARN("GLDevice") << "GL_FRAMEBUFFER_SRGB not supported; output gamma may mismatch VK";
        }

        FillCaps();
        m_commandList = std::make_unique<GLCommandList>(this);
        return true;
    }

    bool GLDevice::OnInitSwapchain(IWindow* window)
    {
        // OpenGL 没有真正的 swapchain；记录默认 backbuffer 尺寸 + 设置默认 viewport
        if (window)
        {
            m_defaultWidth = window->GetWidth();
            m_defaultHeight = window->GetHeight();
            glViewport(0, 0, static_cast<GLsizei>(m_defaultWidth), static_cast<GLsizei>(m_defaultHeight));
        }
        return true;
    }

    void GLDevice::OnShutdownSwapchain()
    {
        // GL 默认 backbuffer 由 IWindow 持有；本方法仅清缓存
        m_defaultWidth = 0;
        m_defaultHeight = 0;
    }

    void GLDevice::OnShutdownBackend()
    {
        glFinish();

        for (auto& kv : m_pipelines)
        {
            if (kv.second.program)
                glDeleteProgram(kv.second.program);
            if (kv.second.vao)
                glDeleteVertexArrays(1, &kv.second.vao);
        }
        m_pipelines.clear();

        for (auto& kv : m_shaders)
            if (kv.second.id)
                glDeleteShader(kv.second.id);
        m_shaders.clear();

        for (auto& kv : m_samplers)
            if (kv.second.id)
                glDeleteSamplers(1, &kv.second.id);
        m_samplers.clear();

        for (auto& kv : m_renderTargets)
            if (kv.second.fbo)
                glDeleteFramebuffers(1, &kv.second.fbo);
        m_renderTargets.clear();

        for (auto& kv : m_textures)
            if (kv.second.id) glDeleteTextures(1, &kv.second.id);
        m_textures.clear();

        for (auto& kv : m_buffers)
            if (kv.second.id)
                glDeleteBuffers(1, &kv.second.id);
        m_buffers.clear();

        m_commandList.reset();
    }

    // ------------------------------------------------------------------------
    // WaitIdle / OnWindowResized
    // ------------------------------------------------------------------------
    void GLDevice::OnWaitIdleImpl()
    {
        glFinish();
    }

    void GLDevice::OnWindowResizedImpl(uint32_t width, uint32_t height)
    {
        // GL 后端的窗口尺寸变化只更新默认 viewport 即可，不需要重建 swapchain
        m_defaultWidth = width;
        m_defaultHeight = height;
        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }

    // ------------------------------------------------------------------------
    // 能力查询：填充基类的 m_caps
    // ------------------------------------------------------------------------
    void GLDevice::FillCaps()
    {
        GLint v = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
        m_caps.maxTextureSize2D = static_cast<uint32_t>(v);
        glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &v);
        m_caps.maxTextureSize3D = static_cast<uint32_t>(v);
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &v);
        m_caps.maxTextureSizeCube = static_cast<uint32_t>(v);
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &v);
        m_caps.maxColorAttachments = static_cast<uint32_t>(v);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v);
        m_caps.maxVertexAttributes = static_cast<uint32_t>(v);
        m_caps.maxBoundDescriptorSets = 0;
        m_caps.supportsAnisotropy = GLEW_EXT_texture_filter_anisotropic ? true : false;
        m_caps.supportsGeometryShader = true;
        m_caps.supportsTessellation = true;
        m_caps.supportsMultiDrawIndirect = true;
        const GLubyte* name = glGetString(GL_RENDERER);
        if (name) m_caps.deviceName = reinterpret_cast<const char*>(name);
    }

    // ------------------------------------------------------------------------
    // 资源 *Impl()：基类已分配 id，子类只关注 GLuint 创建与映射
    // ------------------------------------------------------------------------
    bool GLDevice::CreateBufferImpl(uint64_t id, const BufferDesc& desc)
    {
        GLBufferEntry e{};
        e.target = PickGLBufferTarget(desc.usage);
        e.usage = PickGLBufferUsageHint(desc.memory);
        e.size = desc.size;
        e.mappable = (desc.memory == MemoryUsage::CpuToGpu || desc.memory == MemoryUsage::CpuOnly);

        glGenBuffers(1, &e.id);
        glBindBuffer(e.target, e.id);
        glBufferData(e.target, static_cast<GLsizeiptr>(desc.size), desc.initialData, e.usage);
        glBindBuffer(e.target, 0);

        m_buffers.emplace(id, e);
        return true;
    }

    void GLDevice::DeleteBufferImpl(uint64_t id)
    {
        auto it = m_buffers.find(id);
        if (it == m_buffers.end()) return;
        if (it->second.id)
            glDeleteBuffers(1, &it->second.id);
        m_buffers.erase(it);
    }

    void GLDevice::UpdateBufferImpl(BufferHandle h, const void* src, size_t bytes, size_t dstOffset)
    {
        auto it = m_buffers.find(h.id);
        if (it == m_buffers.end()) return;
        glBindBuffer(it->second.target, it->second.id);
        glBufferSubData(it->second.target,
                        static_cast<GLintptr>(dstOffset),
                        static_cast<GLsizeiptr>(bytes), src);
        glBindBuffer(it->second.target, 0);
    }

    bool GLDevice::CreateTextureImpl(uint64_t id, const TextureDesc& desc)
    {
        GLTextureEntry e{};
        switch (desc.type)
        {
        case TextureType::Tex1D: e.target = GL_TEXTURE_1D;
            break;
        case TextureType::Tex2D: e.target = GL_TEXTURE_2D;
            break;
        case TextureType::Tex3D: e.target = GL_TEXTURE_3D;
            break;
        case TextureType::TexCube: e.target = GL_TEXTURE_CUBE_MAP;
            break;
        case TextureType::Tex2DArray: e.target = GL_TEXTURE_2D_ARRAY;
            break;
        }
        e.internalFmt = ToGLInternalFormat(desc.format);
        e.dataFmt = ToGLDataFormat(desc.format);
        e.dataType = ToGLDataType(desc.format);
        e.width = desc.width;
        e.height = desc.height;
        e.depth = desc.depth;
        // mipLevels=0 在 GDescs 中约定为"由后端按 max 计算"。
        // glTexStorage2D 要求 levels >= 1；这里展开为 floor(log2(max(w,h)))+1。
        // 同时 e.mipLevels 也写入展开后的真实层数，方便后续上传/查询。
        uint32_t resolvedMips = desc.mipLevels;
        if (resolvedMips == 0)
        {
            uint32_t m = std::max(desc.width, std::max(desc.height, desc.depth));
            resolvedMips = 1;
            while (m > 1)
            {
                m >>= 1;
                ++resolvedMips;
            }
        }
        e.mipLevels = resolvedMips;
        e.arrayLayers = desc.arrayLayers;
        e.isDepth = HasFlag(desc.usage, TextureUsage::DepthStencilAttachment);

        glGenTextures(1, &e.id);
        glBindTexture(e.target, e.id);
        if (e.target == GL_TEXTURE_2D)
        {
            glTexStorage2D(e.target, static_cast<GLsizei>(resolvedMips), e.internalFmt, desc.width, desc.height);
        }
        else if (e.target == GL_TEXTURE_3D)
        {
            glTexStorage3D(e.target, static_cast<GLsizei>(resolvedMips), e.internalFmt, desc.width, desc.height, desc.depth);
        }
        else if (e.target == GL_TEXTURE_2D_ARRAY)
        {
            glTexStorage3D(e.target, static_cast<GLsizei>(resolvedMips), e.internalFmt, desc.width, desc.height, desc.arrayLayers);
        }
        else if (e.target == GL_TEXTURE_CUBE_MAP)
        {
            glTexStorage2D(e.target, static_cast<GLsizei>(resolvedMips), e.internalFmt, desc.width, desc.height);
        }
        glBindTexture(e.target, 0);

        m_textures.emplace(id, e);
        return true;
    }

    void GLDevice::DeleteTextureImpl(uint64_t id)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end()) return;
        if (it->second.id) glDeleteTextures(1, &it->second.id);
        m_textures.erase(it);
    }

    void GLDevice::UpdateTextureImpl(TextureHandle h, const TextureUploadDesc& upload)
    {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end()) return;
        const GLTextureEntry& e = it->second;
        glBindTexture(e.target, e.id);
        if (e.target == GL_TEXTURE_2D)
        {
            glTexSubImage2D(e.target, static_cast<GLint>(upload.mipLevel),
                            static_cast<GLint>(upload.offsetX), static_cast<GLint>(upload.offsetY),
                            static_cast<GLsizei>(upload.width ? upload.width : e.width),
                            static_cast<GLsizei>(upload.height ? upload.height : e.height),
                            e.dataFmt, e.dataType, upload.data);
        }
        else if (e.target == GL_TEXTURE_3D || e.target == GL_TEXTURE_2D_ARRAY)
        {
            glTexSubImage3D(e.target, static_cast<GLint>(upload.mipLevel),
                            static_cast<GLint>(upload.offsetX),
                            static_cast<GLint>(upload.offsetY),
                            static_cast<GLint>(upload.offsetZ + upload.arrayLayer),
                            static_cast<GLsizei>(upload.width ? upload.width : e.width),
                            static_cast<GLsizei>(upload.height ? upload.height : e.height),
                            static_cast<GLsizei>(upload.depth ? upload.depth : 1),
                            e.dataFmt, e.dataType, upload.data);
        }
        // 若该纹理 storage 申请了 >1 个 mip level，但本次上传的是 mip0 且
        // 只上传了一层 → 让驱动自动生成完整 mip 链；否则采样器在 LINEAR_MIPMAP_*
        // 过滤下会遇到 incomplete texture（高 level 全部为 0×0）。
        if (e.mipLevels > 1 && upload.mipLevel == 0)
        {
            glGenerateMipmap(e.target);
        }
        glBindTexture(e.target, 0);
    }

    bool GLDevice::CreateSamplerImpl(uint64_t id, const SamplerDesc& desc)
    {
        GLSamplerEntry e{};
        glGenSamplers(1, &e.id);
        const bool hasMipmap = desc.maxLod > desc.minLod;
        glSamplerParameteri(e.id, GL_TEXTURE_MIN_FILTER,
                            ToGLMinFilter(desc.minFilter, desc.mipmapMode, hasMipmap));
        glSamplerParameteri(e.id, GL_TEXTURE_MAG_FILTER, ToGLMagFilter(desc.magFilter));
        glSamplerParameteri(e.id, GL_TEXTURE_WRAP_S, ToGLAddressMode(desc.addressU));
        glSamplerParameteri(e.id, GL_TEXTURE_WRAP_T, ToGLAddressMode(desc.addressV));
        glSamplerParameteri(e.id, GL_TEXTURE_WRAP_R, ToGLAddressMode(desc.addressW));
        glSamplerParameterf(e.id, GL_TEXTURE_MIN_LOD, desc.minLod);
        glSamplerParameterf(e.id, GL_TEXTURE_MAX_LOD, desc.maxLod);
        glSamplerParameterf(e.id, GL_TEXTURE_LOD_BIAS, desc.mipLodBias);
        if (desc.compareEnable)
        {
            glSamplerParameteri(e.id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glSamplerParameteri(e.id, GL_TEXTURE_COMPARE_FUNC, ToGLCompareOp(desc.compareOp));
        }
        if (desc.anisotropyEnable && GLEW_EXT_texture_filter_anisotropic)
        {
            glSamplerParameterf(e.id, GL_TEXTURE_MAX_ANISOTROPY_EXT, desc.maxAnisotropy);
        }

        m_samplers.emplace(id, e);
        return true;
    }

    void GLDevice::DeleteSamplerImpl(uint64_t id)
    {
        auto it = m_samplers.find(id);
        if (it == m_samplers.end()) return;
        if (it->second.id)
            glDeleteSamplers(1, &it->second.id);
        m_samplers.erase(it);
    }

    bool GLDevice::CreateShaderImpl(uint64_t id, const ShaderDesc& desc)
    {
        GLShaderEntry e{};
        e.stage = ToGLShaderStage(desc.stage);
        e.reflection = desc.reflection;
        e.id = glCreateShader(e.stage);

        const GLchar* src = static_cast<const GLchar*>(desc.code);
        const GLint srcLen = static_cast<GLint>(desc.bytes);
        glShaderSource(e.id, 1, &src, &srcLen);
        glCompileShader(e.id);

        GLint ok = GL_FALSE;
        glGetShaderiv(e.id, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint logLen = 0;
            glGetShaderiv(e.id, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(static_cast<size_t>(logLen) + 1, '\0');
            glGetShaderInfoLog(e.id, logLen, nullptr, log.data());
            LOG_STREAM_ERROR("GLDevice") << "Shader compile failed:\n" << log.data();
            glDeleteShader(e.id);
            return false;
        }

        m_shaders.emplace(id, e);
        return true;
    }

    void GLDevice::DeleteShaderImpl(uint64_t id)
    {
        auto it = m_shaders.find(id);
        if (it == m_shaders.end()) return;
        if (it->second.id)
            glDeleteShader(it->second.id);
        m_shaders.erase(it);
    }

    // ------------------------------------------------------------------------
    // Pipeline 创建：OpenGL 用 Program + VAO 模拟 PSO
    // ------------------------------------------------------------------------
    bool GLDevice::CreatePipelineImpl(uint64_t id, const GraphicsPipelineDesc& desc)
    {
        GLPipelineEntry pe{};
        pe.topology = desc.topology;
        pe.rasterizer = desc.rasterizer;
        pe.depthStencil = desc.depthStencil;
        pe.blend = desc.blend;
        pe.vertexLayout = desc.vertexLayout;
        pe.resourceBindings = desc.resourceBindings;
        pe.pushConstantRanges = desc.pushConstantRanges;

        // 1) Program：链接 vs/fs（可选 gs）
        pe.program = glCreateProgram();
        auto attachIfValid = [&](ShaderHandle h)
        {
            if (!h.IsValid()) return;
            const GLShaderEntry* se = LookupShader(h);
            if (!se) return;
            glAttachShader(pe.program, se->id);
        };
        attachIfValid(desc.vertexShader);
        attachIfValid(desc.fragmentShader);
        attachIfValid(desc.geometryShader);
        glLinkProgram(pe.program);

        GLint ok = GL_FALSE;
        glGetProgramiv(pe.program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            GLint logLen = 0;
            glGetProgramiv(pe.program, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(static_cast<size_t>(logLen) + 1, '\0');
            glGetProgramInfoLog(pe.program, logLen, nullptr, log.data());
            LOG_STREAM_ERROR("GLDevice") << "Program link failed:\n" << log.data();
            glDeleteProgram(pe.program);
            return false;
        }

        // 2) VAO：根据 vertexLayout 把 attribute 绑到指定 binding
        glGenVertexArrays(1, &pe.vao);
        glBindVertexArray(pe.vao);
        for (const auto& vb : desc.vertexLayout.bindings)
        {
            glVertexBindingDivisor(vb.binding,
                                   vb.inputRate == VertexInputRate::Instance ? 1 : 0);
        }
        for (const auto& va : desc.vertexLayout.attributes)
        {
            glEnableVertexAttribArray(va.location);
            glVertexAttribFormat(va.location,
                                 (va.format == Format::R32_SFLOAT)
                                     ? 1
                                     : (va.format == Format::R32G32_SFLOAT)
                                     ? 2
                                     : (va.format == Format::R32G32B32_SFLOAT)
                                     ? 3
                                     : (va.format == Format::R32G32B32A32_SFLOAT)
                                     ? 4
                                     : 4,
                                 ToGLDataType(va.format),
                                 GL_FALSE,
                                 va.offset);
            glVertexAttribBinding(va.location, va.binding);
        }
        glBindVertexArray(0);

        // 3) UBO / Sampler / StorageImage 反射映射
        //    - UniformBuffer：block index → binding（glUniformBlockBinding）
        //    - SampledTexture / CombinedImageSampler：sampler uniform → texture unit
        //      (GLSL 默认所有 sampler 都从 unit 0 采样；必须显式 glUniform1i(loc, unit)
        //       才能把每个 sampler uniform 绑到 ResourceSet 中声明的 binding 单元，
        //       否则多张 sampler 会全部"看见"unit 0 上的同一张纹理)。
        //    - StorageTexture：image2D uniform → image unit（同样需要 glUniform1i）。
        glUseProgram(pe.program);
        for (const auto& rb : desc.resourceBindings)
        {
            if (rb.name.empty()) continue;
            switch (rb.type)
            {
            case ResourceBindingType::UniformBuffer:
                {
                    GLuint blockIdx = glGetUniformBlockIndex(pe.program, rb.name.c_str());
                    if (blockIdx != GL_INVALID_INDEX)
                        glUniformBlockBinding(pe.program, blockIdx, rb.binding);
                    break;
                }
            case ResourceBindingType::SampledTexture:
            case ResourceBindingType::CombinedImageSampler:
            case ResourceBindingType::StorageTexture:
                {
                    GLint loc = glGetUniformLocation(pe.program, rb.name.c_str());
                    if (loc >= 0)
                        glUniform1i(loc, static_cast<GLint>(rb.binding));
                    break;
                }
            default:
                break;
            }
        }
        glUseProgram(0);

        m_pipelines.emplace(id, pe);
        return true;
    }

    void GLDevice::DeletePipelineImpl(uint64_t id)
    {
        auto it = m_pipelines.find(id);
        if (it == m_pipelines.end()) return;
        if (it->second.program)
            glDeleteProgram(it->second.program);
        if (it->second.vao)
            glDeleteVertexArrays(1, &it->second.vao);
        m_pipelines.erase(it);
    }

    // ------------------------------------------------------------------------
    // Compute Pipeline 创建
    //   - GL 没有真正的 PSO；compute pipeline 仅 attach 一个 GL_COMPUTE_SHADER。
    //   - 仍写入 m_pipelines（同一句柄空间），通过 isCompute=true 标记，
    //     GLCommandList 在 BindPipeline / Dispatch 时按此分支处理（不绑 VAO，
    //     不开光栅化/深度测试）。
    // ------------------------------------------------------------------------
    bool GLDevice::CreatePipelineImpl(uint64_t id, const ComputePipelineDesc& desc)
    {
        if (!desc.computeShader.IsValid())
        {
            LOG_STREAM_ERROR("GLDevice") << "CreatePipeline(Compute): cs not set";
            return false;
        }
        const GLShaderEntry* cs = LookupShader(desc.computeShader);
        if (!cs || cs->stage != GL_COMPUTE_SHADER)
        {
            LOG_STREAM_ERROR("GLDevice") << "CreatePipeline(Compute): shader handle not a compute shader";
            return false;
        }

        GLPipelineEntry pe{};
        pe.isCompute = true;
        pe.resourceBindings = desc.resourceBindings;
        pe.pushConstantRanges = desc.pushConstantRanges;

        pe.program = glCreateProgram();
        glAttachShader(pe.program, cs->id);
        glLinkProgram(pe.program);

        GLint ok = GL_FALSE;
        glGetProgramiv(pe.program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            GLint logLen = 0;
            glGetProgramiv(pe.program, GL_INFO_LOG_LENGTH, &logLen);
            std::vector<char> log(static_cast<size_t>(logLen) + 1, '\0');
            glGetProgramInfoLog(pe.program, logLen, nullptr, log.data());
            LOG_STREAM_ERROR("GLDevice") << "Compute program link failed:\n" << log.data();
            glDeleteProgram(pe.program);
            return false;
        }

        // UBO / Sampler / StorageImage 反射映射（与 graphics pipeline 一致）
        glUseProgram(pe.program);
        for (const auto& rb : desc.resourceBindings)
        {
            if (rb.name.empty()) continue;
            switch (rb.type)
            {
            case ResourceBindingType::UniformBuffer:
                {
                    GLuint blockIdx = glGetUniformBlockIndex(pe.program, rb.name.c_str());
                    if (blockIdx != GL_INVALID_INDEX)
                        glUniformBlockBinding(pe.program, blockIdx, rb.binding);
                    break;
                }
            case ResourceBindingType::SampledTexture:
            case ResourceBindingType::CombinedImageSampler:
            case ResourceBindingType::StorageTexture:
                {
                    GLint loc = glGetUniformLocation(pe.program, rb.name.c_str());
                    if (loc >= 0)
                        glUniform1i(loc, static_cast<GLint>(rb.binding));
                    break;
                }
            default:
                break;
            }
        }
        glUseProgram(0);

        m_pipelines.emplace(id, pe);
        return true;
    }

    // ------------------------------------------------------------------------
    // RenderTarget
    // ------------------------------------------------------------------------
    bool GLDevice::CreateRenderTargetImpl(uint64_t id, const RenderTargetDesc& desc)
    {
        GLRenderTargetEntry rt{};
        rt.width = desc.width;
        rt.height = desc.height;
        rt.colorAttachments.reserve(desc.colorAttachments.size());

        glGenFramebuffers(1, &rt.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);

        std::vector<GLenum> drawBuffers;
        for (size_t i = 0; i < desc.colorAttachments.size(); ++i)
        {
            const auto& a = desc.colorAttachments[i];
            const GLTextureEntry* te = LookupTexture(a.texture);
            if (!te) continue;
            const GLenum att = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
            glFramebufferTexture2D(GL_FRAMEBUFFER, att, te->target, te->id,
                                   static_cast<GLint>(a.mipLevel));
            drawBuffers.push_back(att);
            rt.colorAttachments.push_back(a.texture);
        }
        if (desc.depthStencilAttachment.texture.IsValid())
        {
            const GLTextureEntry* te = LookupTexture(desc.depthStencilAttachment.texture);
            if (te)
            {
                const GLenum att = (te->internalFmt == GL_DEPTH24_STENCIL8 ||
                                       te->internalFmt == GL_DEPTH32F_STENCIL8)
                                       ? GL_DEPTH_STENCIL_ATTACHMENT
                                       : GL_DEPTH_ATTACHMENT;
                glFramebufferTexture2D(GL_FRAMEBUFFER, att, te->target, te->id,
                                       static_cast<GLint>(desc.depthStencilAttachment.mipLevel));
                rt.depthStencilAttachment = desc.depthStencilAttachment.texture;
            }
        }
        if (!drawBuffers.empty())
            glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            LOG_STREAM_ERROR("GLDevice") << "FBO incomplete: 0x" << std::hex << status << std::dec;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        m_renderTargets.emplace(id, rt);
        return true;
    }

    void GLDevice::DeleteRenderTargetImpl(uint64_t id)
    {
        auto it = m_renderTargets.find(id);
        if (it == m_renderTargets.end()) return;
        if (it->second.fbo)
            glDeleteFramebuffers(1, &it->second.fbo);
        m_renderTargets.erase(it);
    }

    // ------------------------------------------------------------------------
    // 帧控制 *Impl()
    // ------------------------------------------------------------------------
    void GLDevice::BeginFrameImpl()
    {
        if (m_commandList) m_commandList->Reset();
    }

    RenderCommandList* GLDevice::AcquireCommandListImpl()
    {
        return m_commandList.get();
    }

    void GLDevice::SubmitImpl(RenderCommandList* /*cmd*/)
    {
        if (m_commandList) m_commandList->Replay();
    }

    void GLDevice::PresentImpl()
    {
        // GL 的 Present 由 IWindow 实现执行 swapBuffers；这里不做任何事，
        // 帧索引推进在基类 Present() 中完成。
    }

    // ------------------------------------------------------------------------
    // 查询
    // ------------------------------------------------------------------------
    const GLBufferEntry* GLDevice::LookupBuffer(BufferHandle h) const
    {
        auto it = m_buffers.find(h.id);
        return it == m_buffers.end() ? nullptr : &it->second;
    }

    const GLTextureEntry* GLDevice::LookupTexture(TextureHandle h) const
    {
        auto it = m_textures.find(h.id);
        return it == m_textures.end() ? nullptr : &it->second;
    }

    const GLSamplerEntry* GLDevice::LookupSampler(SamplerHandle h) const
    {
        auto it = m_samplers.find(h.id);
        return it == m_samplers.end() ? nullptr : &it->second;
    }

    const GLShaderEntry* GLDevice::LookupShader(ShaderHandle h) const
    {
        auto it = m_shaders.find(h.id);
        return it == m_shaders.end() ? nullptr : &it->second;
    }

    const GLPipelineEntry* GLDevice::LookupPipeline(PipelineHandle h) const
    {
        auto it = m_pipelines.find(h.id);
        return it == m_pipelines.end() ? nullptr : &it->second;
    }

    const GLRenderTargetEntry* GLDevice::LookupRenderTarget(RenderTargetHandle h) const
    {
        auto it = m_renderTargets.find(h.id);
        return it == m_renderTargets.end() ? nullptr : &it->second;
    }
}
