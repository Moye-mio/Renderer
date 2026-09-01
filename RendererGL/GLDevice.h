#pragma once
// ============================================================================
// Renderer (OpenGL) - GLDevice
// 继承 RendererCore::GThreadableDevice（后端无关基类）：
//   - 基类承担参数校验、句柄 ID 分配、模板方法骨架（Init / Shutdown / CreateXxx）
//   - 子类只实现 OnInitBackend / OnInitSwapchain / *Impl() 钩子
//   - 维护"GHandle ↔ GLuint"映射表（基类不持有任何后端原生类型）
//   - 内部复用 GLCommandList 的 std::function 延迟队列
//   - WaitIdle 退化为 glFinish
// ============================================================================
#include <GL/glew.h>
#include <unordered_map>
#include <memory>
#include <vector>

#include "RendererCore/GThreadableDevice.h"

namespace TitusGraphics
{
    class GLCommandList;

    // ------------------------------------------------------------------------
    // 资源条目
    // ------------------------------------------------------------------------
    struct GLBufferEntry
    {
        GLuint id = 0;
        GLenum target = GL_ARRAY_BUFFER; // 主 target（用于绑定）
        GLenum usage = GL_STATIC_DRAW; // GL_STATIC_DRAW / GL_DYNAMIC_DRAW ...
        uint64_t size = 0;
        bool mappable = false; // CpuToGpu 时为 true（持久映射 / glBufferSubData）
        void* persistentPtr = nullptr; // 暂未启用（GL 4.4 持久映射可后续接入）
    };

    struct GLTextureEntry
    {
        GLuint id = 0;
        GLenum target = GL_TEXTURE_2D;
        GLenum internalFmt = GL_RGBA8;
        GLenum dataFmt = GL_RGBA;
        GLenum dataType = GL_UNSIGNED_BYTE;
        uint32_t width = 0, height = 0, depth = 1;
        uint32_t mipLevels = 1, arrayLayers = 1;
        uint32_t samples = 1;
        bool isDepth = false;
    };

    struct GLSamplerEntry
    {
        GLuint id = 0;
    };

    struct GLShaderEntry
    {
        GLuint id = 0; // glCreateShader 产物
        GLenum stage = GL_VERTEX_SHADER;
        TitusRHI::ReflectionInfo reflection;
    };

    struct GLPipelineEntry
    {
        // OpenGL 没有真正的 PSO；此处用 ProgramPipeline 或 Program 模拟
        GLuint program = 0; // 链接后的 GL 程序
        TitusRHI::PrimitiveTopology topology = TitusRHI::PrimitiveTopology::TriangleList;
        TitusRHI::RasterizerState rasterizer;
        TitusRHI::DepthStencilState depthStencil;
        TitusRHI::BlendState blend;
        TitusRHI::VertexLayout vertexLayout;
        std::vector<TitusRHI::ResourceBinding> resourceBindings;
        std::vector<TitusRHI::PushConstantRange> pushConstantRanges;
        // VAO：根据 vertexLayout 创建（与 program 解耦），按 binding+attribute 配置
        GLuint vao = 0;
        // 是否为计算管线（program 仅 attach 了 compute shader）
        bool isCompute = false;
    };

    struct GLRenderTargetEntry
    {
        GLuint fbo = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<TitusRHI::TextureHandle> colorAttachments;
        TitusRHI::TextureHandle depthStencilAttachment;
    };

    // ------------------------------------------------------------------------
    // GLDevice —— 继承 GThreadableDevice 的 OpenGL 子类
    // ------------------------------------------------------------------------
    class GLDevice : public TitusRHI::GThreadableDevice
    {
    public:
        GLDevice();
        ~GLDevice() override;

        GLDevice(const GLDevice&) = delete;
        GLDevice& operator=(const GLDevice&) = delete;

        // ====================================================================
        // 后端 / 能力查询（IGDevice 接口）
        // ====================================================================
        TitusRHI::GBackend GetBackend() const override { return TitusRHI::GBackend::OpenGL; }

        // Overlay 录制 Hook（详见 IGDevice.h）。
        // GL 后端实现：ScreenQuadPass 已结束 RP（默认 FB 仍绑定），
        // 直接调用注入的 callback —— 内部会执行 ImGui_ImplOpenGL3_RenderDrawData。
        void SetImGuiOverlayCallback(ImGuiOverlayCallback cb, void* userData) override
        {
            m_imGuiCallback = cb;
            m_imGuiUserData = userData;
        }
        void RenderImGuiOverlay() override
        {
            if (m_imGuiCallback) m_imGuiCallback(m_imGuiUserData);
        }

        // ====================================================================
        // 后端内部访问（仅 GLCommandList 使用）
        // ====================================================================
        const GLBufferEntry* LookupBuffer(TitusRHI::BufferHandle h) const;
        const GLTextureEntry* LookupTexture(TitusRHI::TextureHandle h) const;
        const GLSamplerEntry* LookupSampler(TitusRHI::SamplerHandle h) const;
        const GLShaderEntry* LookupShader(TitusRHI::ShaderHandle h) const;
        const GLPipelineEntry* LookupPipeline(TitusRHI::PipelineHandle h) const;
        const GLRenderTargetEntry* LookupRenderTarget(TitusRHI::RenderTargetHandle h) const;

        // 当前默认后缓冲区尺寸
        uint32_t GetDefaultWidth() const { return m_defaultWidth; }
        uint32_t GetDefaultHeight() const { return m_defaultHeight; }

        // 读回默认 FBO（须在本帧绘制完成之后、SwapBuffers 之前调用）
        bool ReadbackBackbuffer(std::vector<uint8_t>& outRgba,
                                uint32_t& outWidth,
                                uint32_t& outHeight) override;

    protected:
        // ====================================================================
        // GThreadableDevice / GDevice 钩子
        // ====================================================================
        // 后端创建：GLFW makeContextCurrent 假定由 IWindow 完成；本方法负责
        // glewInit / 查询 GCaps。
        bool OnInitBackend(const TitusRHI::GDeviceDesc& desc, TitusRHI::IWindow* window) override;
        bool OnInitSwapchain(TitusRHI::IWindow* window) override;
        void OnShutdownSwapchain() override;
        void OnShutdownBackend() override;

        void OnWaitIdleImpl() override;
        void OnWindowResizedImpl(uint32_t width, uint32_t height) override;

        // 资源 *Impl()：基类已分配 id；子类只需把 id ↔ GLuint 关联到映射表
        bool CreateBufferImpl(uint64_t id, const TitusRHI::BufferDesc& desc) override;
        bool CreateTextureImpl(uint64_t id, const TitusRHI::TextureDesc& desc) override;
        bool CreateSamplerImpl(uint64_t id, const TitusRHI::SamplerDesc& desc) override;
        bool CreateShaderImpl(uint64_t id, const TitusRHI::ShaderDesc& desc) override;
        bool CreatePipelineImpl(uint64_t id, const TitusRHI::GraphicsPipelineDesc& desc) override;
        // 计算管线创建
        bool CreatePipelineImpl(uint64_t id, const TitusRHI::ComputePipelineDesc& desc) override;
        bool CreateRenderTargetImpl(uint64_t id, const TitusRHI::RenderTargetDesc& desc) override;

        void DeleteBufferImpl(uint64_t id) override;
        void DeleteTextureImpl(uint64_t id) override;
        void DeleteSamplerImpl(uint64_t id) override;
        void DeleteShaderImpl(uint64_t id) override;
        void DeletePipelineImpl(uint64_t id) override;
        void DeleteRenderTargetImpl(uint64_t id) override;

        // 上传 *Impl()
        void UpdateBufferImpl(TitusRHI::BufferHandle buffer,
                              const void* src,
                              size_t bytes,
                              size_t dstOffset) override;
        void UpdateTextureImpl(TitusRHI::TextureHandle texture,
                               const TitusRHI::TextureUploadDesc& upload) override;

        // 帧控制 *Impl()
        void BeginFrameImpl() override;
        TitusRHI::RenderCommandList* AcquireCommandListImpl() override;
        void SubmitImpl(TitusRHI::RenderCommandList* cmd) override;
        void PresentImpl() override;

    private:
        void FillCaps();

    private:
        std::unique_ptr<GLCommandList> m_commandList;

        std::unordered_map<uint64_t, GLBufferEntry> m_buffers;
        std::unordered_map<uint64_t, GLTextureEntry> m_textures;
        std::unordered_map<uint64_t, GLSamplerEntry> m_samplers;
        std::unordered_map<uint64_t, GLShaderEntry> m_shaders;
        std::unordered_map<uint64_t, GLPipelineEntry> m_pipelines;
        std::unordered_map<uint64_t, GLRenderTargetEntry> m_renderTargets;

        uint32_t m_defaultWidth = 0;
        uint32_t m_defaultHeight = 0;

        // Overlay 回调（由 RendererInterface 的 IMGUI 模块注入）
        ImGuiOverlayCallback m_imGuiCallback = nullptr;
        void*                m_imGuiUserData = nullptr;
    };
}
