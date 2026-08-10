#pragma once
// ============================================================================
// RendererCore - GDescs
// 后端无关的描述结构体集合：
//  - 资源描述：BufferDesc / TextureDesc / SamplerDesc / ShaderDesc / RenderTargetDesc
//  - 管线状态：VertexLayout / RasterizerState / DepthStencilState / BlendState / GraphicsPipelineDesc
//  - 渲染通道：RenderPassBeginInfo / Viewport / Rect2D / ClearValue
//  - 设备级：GDeviceDesc / GCaps
// ============================================================================
#include <cstdint>
#include <array>
#include <vector>
#include <string>

#include "GEnums.h"
#include "GHandle.h"

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // 通用几何 / 视口
    // ------------------------------------------------------------------------
    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    struct Rect2D
    {
        int32_t offsetX = 0;
        int32_t offsetY = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct Extent2D
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    // ------------------------------------------------------------------------
    // ClearValue —— RenderPassBeginInfo 内部使用的清屏值
    // 同时承载颜色与深度/模板（后端按附件类型选取对应字段）
    // ------------------------------------------------------------------------
    struct ClearValue
    {
        // 颜色清屏：r,g,b,a
        float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        // 深度/模板清屏
        float depth = 1.0f;
        uint32_t stencil = 0;
    };

    // ------------------------------------------------------------------------
    // Buffer
    // ------------------------------------------------------------------------
    struct BufferDesc
    {
        uint64_t size = 0;
        BufferUsage usage = BufferUsage::None;
        MemoryUsage memory = MemoryUsage::GpuOnly;
        // 可选：初始数据指针（nullptr 表示不上传初始数据）
        const void* initialData = nullptr;
        const char* debugName = nullptr;
    };

    // ------------------------------------------------------------------------
    // Texture
    // ------------------------------------------------------------------------
    struct TextureDesc
    {
        TextureType type = TextureType::Tex2D;
        Format format = Format::R8G8B8A8_UNORM;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1; // 仅 Tex3D 使用
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1; // Cube=6 / Array>=1
        uint32_t samples = 1; // MSAA
        TextureUsage usage = TextureUsage::Sampled;
        const char* debugName = nullptr;
    };

    // 单次纹理上传请求
    struct TextureUploadDesc
    {
        const void* data = nullptr;
        size_t bytes = 0;
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t offsetX = 0;
        uint32_t offsetY = 0;
        uint32_t offsetZ = 0;
    };

    // ------------------------------------------------------------------------
    // Sampler
    // ------------------------------------------------------------------------
    struct SamplerDesc
    {
        FilterMode minFilter = FilterMode::Linear;
        FilterMode magFilter = FilterMode::Linear;
        MipmapMode mipmapMode = MipmapMode::Linear;
        AddressMode addressU = AddressMode::Repeat;
        AddressMode addressV = AddressMode::Repeat;
        AddressMode addressW = AddressMode::Repeat;
        float minLod = 0.0f;
        float maxLod = 1000.0f;
        float mipLodBias = 0.0f;
        bool anisotropyEnable = false;
        float maxAnisotropy = 1.0f;
        bool compareEnable = false;
        CompareOp compareOp = CompareOp::Never;
        float borderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const char* debugName = nullptr;
    };

    // ------------------------------------------------------------------------
    // Shader 反射信息（后端用来构建 VkDescriptorSetLayout 或 GL Uniform/UBO 绑定）
    // ------------------------------------------------------------------------
    enum class ResourceBindingType : uint8_t
    {
        UniformBuffer = 0,
        StorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        CombinedImageSampler,
        AccelerationStructure,
    };

    struct ResourceBinding
    {
        std::string name;
        uint32_t set = 0;
        uint32_t binding = 0;
        uint32_t count = 1;
        ResourceBindingType type = ResourceBindingType::UniformBuffer;
        ShaderStage stages = ShaderStage::None;
    };

    struct PushConstantRange
    {
        ShaderStage stages = ShaderStage::None;
        uint32_t offset = 0;
        uint32_t size = 0;
        // GL 后端用 push_constant 反射回 glUniform 时，按 (offset, size) 锁定一个
        // sub-range；通过本字段 glName 直接查找对应的传统 uniform 名（如 "u_ModelMatrix"）。
        // VK 后端忽略本字段。Empty 表示走默认 fallback（按 PC_<offset> 命名查找）。
        std::string glName;
    };

    struct ReflectionInfo
    {
        std::vector<ResourceBinding> bindings;
        std::vector<PushConstantRange> pushConstants;
    };

    // ------------------------------------------------------------------------
    // Shader
    // ------------------------------------------------------------------------
    struct ShaderDesc
    {
        ShaderStage stage = ShaderStage::None;
        const void* code = nullptr; // GL 后端塞 GLSL 文本，VK 后端塞 SPIR-V 字节码
        size_t bytes = 0;
        const char* entryPoint = "main";
        ReflectionInfo reflection;
        const char* debugName = nullptr;
    };

    // ------------------------------------------------------------------------
    // VertexLayout
    // ------------------------------------------------------------------------
    enum class VertexInputRate : uint8_t
    {
        Vertex = 0,
        Instance,
    };

    struct VertexAttribute
    {
        uint32_t location = 0; // shader location
        uint32_t binding = 0; // 引用 VertexBinding.binding
        Format format = Format::R32G32B32_SFLOAT;
        uint32_t offset = 0; // 单个顶点结构体内的字节偏移
    };

    struct VertexBinding
    {
        uint32_t binding = 0;
        uint32_t stride = 0;
        VertexInputRate inputRate = VertexInputRate::Vertex;
    };

    struct VertexLayout
    {
        std::vector<VertexBinding> bindings;
        std::vector<VertexAttribute> attributes;
    };

    // ------------------------------------------------------------------------
    // 管线状态
    // ------------------------------------------------------------------------
    struct RasterizerState
    {
        PolygonMode polygonMode = PolygonMode::Fill;
        CullMode cullMode = CullMode::Back;
        FrontFace frontFace = FrontFace::CounterClockwise;
        bool depthClampEnable = false;
        bool rasterizerDiscardEnable = false;
        float lineWidth = 1.0f;
    };

    struct DepthStencilState
    {
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;
        bool stencilTestEnable = false;
    };

    struct BlendAttachmentState
    {
        bool blendEnable = false;
        BlendFactor srcColorBlendFactor = BlendFactor::One;
        BlendFactor dstColorBlendFactor = BlendFactor::Zero;
        BlendOp colorBlendOp = BlendOp::Add;
        BlendFactor srcAlphaBlendFactor = BlendFactor::One;
        BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
        BlendOp alphaBlendOp = BlendOp::Add;
        // 颜色写入掩码：bit0=R, bit1=G, bit2=B, bit3=A
        uint8_t colorWriteMask = 0x0F;
    };

    struct BlendState
    {
        std::vector<BlendAttachmentState> attachments;
    };

    // ------------------------------------------------------------------------
    // RenderTarget 布局（管线烘焙时需要知道 RT 的色/深格式数量）
    // ------------------------------------------------------------------------
    struct RenderTargetLayout
    {
        std::vector<Format> colorFormats;
        Format depthStencilFormat = Format::Undefined;
        uint32_t samples = 1;
    };

    // ------------------------------------------------------------------------
    // 图形管线 Desc
    // ------------------------------------------------------------------------
    struct GraphicsPipelineDesc
    {
        ShaderHandle vertexShader;
        ShaderHandle fragmentShader;
        // 可选阶段
        ShaderHandle geometryShader;

        VertexLayout vertexLayout;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;

        RasterizerState rasterizer;
        DepthStencilState depthStencil;
        BlendState blend;

        RenderTargetLayout rtLayout;

        // 着色器使用的所有 binding（通常由各 Shader.reflection 合并产出）
        std::vector<ResourceBinding> resourceBindings;
        std::vector<PushConstantRange> pushConstantRanges;

        const char* debugName = nullptr;
    };

    // ------------------------------------------------------------------------
    // 计算管线 Desc
    //   - 仅一个 Compute 阶段着色器；后端需以 ComputePipelineDesc 重载创建管线。
    //   - resourceBindings / pushConstantRanges 语义与 GraphicsPipelineDesc 一致：
    //     描述完整的 descriptor / push constant 布局，供 VK 创建 DescriptorSetLayout +
    //     PipelineLayout，GL 后端用于反射 binding 名。
    // ------------------------------------------------------------------------
    struct ComputePipelineDesc
    {
        ShaderHandle computeShader;
        std::vector<ResourceBinding> resourceBindings;
        std::vector<PushConstantRange> pushConstantRanges;
        const char* debugName = nullptr;
    };

    // ------------------------------------------------------------------------
    // 光追管线
    //   - 后端无关描述：shader stage 集合 + shader group（general / hit）+
    //     maxRayRecursionDepth + resourceBindings + pushConstantRanges。
    //   - 全部字段仅使用 RendererCore 自定义句柄/枚举/POD，禁止出现 VkXxx。
    //   - VK 后端据此调用 vkCreateRayTracingPipelinesKHR 并生成 SBT。
    // ------------------------------------------------------------------------
    enum class RayTracingShaderGroupType : uint8_t
    {
        General = 0,   // raygen / miss / callable：单个 general shader
        TrianglesHit,  // 三角形 hit group：closestHit（+ 可选 anyHit）
        ProceduralHit, // 程序化 hit group：intersection（+ 可选 closestHit/anyHit）
    };

    struct RayTracingShaderStageDesc
    {
        ShaderHandle shader;
        ShaderStage  stage = ShaderStage::RayGen;
    };

    // shader group：各字段为 stages 数组下标；未使用置 kRTUnusedShader。
    // 对应 VK 的 VkRayTracingShaderGroupCreateInfoKHR（VK_SHADER_UNUSED_KHR）。
    static constexpr uint32_t kRTUnusedShader = ~0u;

    struct RayTracingShaderGroupDesc
    {
        RayTracingShaderGroupType type = RayTracingShaderGroupType::General;
        uint32_t generalShader      = kRTUnusedShader; // raygen/miss/callable
        uint32_t closestHitShader   = kRTUnusedShader;
        uint32_t anyHitShader       = kRTUnusedShader;
        uint32_t intersectionShader = kRTUnusedShader;
    };

    struct RayTracingPipelineDesc
    {
        std::vector<RayTracingShaderStageDesc> stages;
        std::vector<RayTracingShaderGroupDesc> groups;
        uint32_t maxRayRecursionDepth = 1;
        std::vector<ResourceBinding>   resourceBindings;
        std::vector<PushConstantRange> pushConstantRanges;
        const char* debugName = nullptr;
    };

    // ------------------------------------------------------------------------
    // 加速结构（光追）
    //   - 后端无关描述：BLAS 从 vertex/index buffer 构建三角形几何；TLAS 由若干
    //     引用 BLAS 的 instance 构成。
    //   - 全部字段仅使用 RendererCore 自定义句柄/枚举/POD，禁止出现 VkXxx。
    //   - VK 后端据此填充 VkAccelerationStructureGeometryKHR 并调用
    //     vkGetAccelerationStructureBuildSizesKHR / vkCmdBuildAccelerationStructuresKHR。
    // ------------------------------------------------------------------------
    enum class AccelerationStructureType : uint8_t
    {
        BottomLevel = 0, // BLAS：几何（三角形网格）
        TopLevel,        // TLAS：instance 场景
    };

    // 构建标志（位标志）。对应 VK 的 VkBuildAccelerationStructureFlagsKHR。
    enum class ASBuildFlags : uint32_t
    {
        None = 0,
        PreferFastTrace = 1u << 0, // 优先追踪性能（静态几何）
        PreferFastBuild = 1u << 1, // 优先构建速度
        AllowUpdate = 1u << 2,     // 允许后续 refit（动态场景）
        AllowCompaction = 1u << 3,
    };

    inline constexpr ASBuildFlags operator|(ASBuildFlags a, ASBuildFlags b)
    {
        return static_cast<ASBuildFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline constexpr bool HasFlag(ASBuildFlags v, ASBuildFlags f)
    {
        return (static_cast<uint32_t>(v) & static_cast<uint32_t>(f)) != 0;
    }

    // BLAS 的单个三角形几何输入
    struct BLASGeometryDesc
    {
        BufferHandle vertexBuffer;                     // 顶点缓冲（需含 ShaderDeviceAddress usage）
        Format vertexFormat = Format::R32G32B32_SFLOAT; // 顶点位置格式
        uint32_t vertexStride = 0;                     // 相邻顶点位置的字节步长
        uint32_t vertexCount = 0;                      // 顶点数（用于 maxVertex）
        uint64_t vertexOffset = 0;                     // 顶点缓冲内起始字节偏移

        BufferHandle indexBuffer;                      // 可选：无索引时留空
        IndexType indexType = IndexType::UInt32;
        uint32_t indexCount = 0;                       // 索引数（三角形数 = indexCount/3）
        uint64_t indexOffset = 0;

        bool opaque = true;                            // 是否 OPAQUE（影响 any-hit）
    };

    // TLAS 的单个 instance（引用一个 BLAS）
    struct TLASInstanceDesc
    {
        AccelerationStructureHandle blas;              // 被引用的 BLAS
        float transform[12] = {                        // 4x3 行主序变换（VkTransformMatrixKHR 布局）
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
        };
        uint32_t instanceCustomIndex = 0;              // gl_InstanceCustomIndexEXT（24 位）
        uint32_t mask = 0xFF;                          // 光线 mask
        uint32_t shaderBindingTableOffset = 0;         // hit group 偏移
        uint32_t flags = 0;                            // VkGeometryInstanceFlagsKHR 语义
    };

    struct AccelerationStructureDesc
    {
        AccelerationStructureType type = AccelerationStructureType::BottomLevel;
        ASBuildFlags buildFlags = ASBuildFlags::PreferFastTrace;

        // type == BottomLevel 时使用
        std::vector<BLASGeometryDesc> geometries;

        // type == TopLevel 时使用
        std::vector<TLASInstanceDesc> instances;

        const char* debugName = nullptr;
    };

    // ------------------------------------------------------------------------
    // RenderTarget Desc
    //  - 描述一组颜色附件 + 可选深度/模板附件
    //  - 业务侧也可传入"使用 Swapchain 的当前 backbuffer"为 nullptr/空表，让后端选择
    // ------------------------------------------------------------------------
    struct RenderTargetAttachmentDesc
    {
        TextureHandle texture; // 引用已创建的纹理
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
    };

    struct RenderTargetDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<RenderTargetAttachmentDesc> colorAttachments;
        RenderTargetAttachmentDesc depthStencilAttachment; // texture.IsValid() 才生效
        const char* debugName = nullptr;
    };

    // ------------------------------------------------------------------------
    // RenderPassBeginInfo
    //  - 在 VK 后端翻译为 vkCmdBeginRenderPass
    //  - 在 GL 后端翻译为 "BindFBO + 按 LoadOp 决定 glClear + 按 StoreOp 决定 glInvalidateFramebuffer"
    // ------------------------------------------------------------------------
    struct RenderPassAttachmentOp
    {
        LoadOp loadOp = LoadOp::Load;
        StoreOp storeOp = StoreOp::Store;
        ClearValue clearValue;
    };

    struct RenderPassBeginInfo
    {
        RenderTargetHandle renderTarget; // 不合法时表示 "默认 backbuffer"
        Rect2D renderArea; // 0 表示按 RT 全尺寸
        std::vector<RenderPassAttachmentOp> colorOps;
        RenderPassAttachmentOp depthStencilOp;
        bool hasDepthStencil = false;
    };

    // ------------------------------------------------------------------------
    // ResourceSet —— 单次绘制时要绑定的 UBO/Texture/Sampler
    // 后端把它转成 VkDescriptorSet 或 GL 的 glBindBufferRange / glBindTextureUnit
    // ------------------------------------------------------------------------
    struct ResourceBindingValue
    {
        uint32_t binding = 0;
        ResourceBindingType type = ResourceBindingType::UniformBuffer;

        // 针对不同 type 选择不同字段
        BufferHandle buffer;
        uint64_t bufferOffset = 0;
        uint64_t bufferRange = 0;

        TextureHandle texture;
        SamplerHandle sampler;

        // 光追：type == AccelerationStructure 时使用。
        AccelerationStructureHandle accelStruct;
    };

    struct ResourceSetDesc
    {
        std::vector<ResourceBindingValue> bindings;
    };

    // ------------------------------------------------------------------------
    // 设备初始化 Desc
    // ------------------------------------------------------------------------
    struct GDeviceDesc
    {
        GBackend backend = GBackend::Unknown;
        bool enableValidation = false;
        const char* applicationName = "TitusApp";
        uint32_t framesInFlight = 2;
        // VK 后端用于创建 VkWindow 的窗口尺寸（0 = 使用后端默认值）
        uint32_t windowWidth  = 0;
        uint32_t windowHeight = 0;
    };

    // ------------------------------------------------------------------------
    // 设备能力描述
    // ------------------------------------------------------------------------
    struct GCaps
    {
        uint32_t maxTextureSize2D = 0;
        uint32_t maxTextureSize3D = 0;
        uint32_t maxTextureSizeCube = 0;
        uint32_t maxColorAttachments = 0;
        uint32_t maxVertexAttributes = 0;
        uint32_t maxBoundDescriptorSets = 0;
        bool supportsAnisotropy = false;
        bool supportsGeometryShader = false;
        bool supportsTessellation = false;
        bool supportsMultiDrawIndirect = false;
        bool supportsBindlessTextures = false;
        // 光追能力：仅当后端探测到硬件光追且编译期开启
        // RENDERER_ENABLE_RAY_TRACING 时为 true；GL / Null 后端恒 false。
        bool supportsRayTracing = false;
        bool supportsRayQuery = false;
        // 光追管线：仅 VK 后端探测到 RT 管线扩展时为 true。
        bool supportsRayTracingPipeline = false;
        std::string deviceName;
    };

    // ------------------------------------------------------------------------
    // PipelineBarrier 描述
    //   - 描述"Storage 写入 → Shader 采样读取"这类依赖。VK 后端按 src/dst stage
    //     + 额外的 ImageBarrier 指定 layout 转换；GL 后端用 glMemoryBarrier(
    //     GL_SHADER_IMAGE_ACCESS_BARRIER_BIT / GL_TEXTURE_FETCH_BARRIER_BIT /
    //     GL_SHADER_STORAGE_BARRIER_BIT) 模拟。
    //   - 以位标志描述访问类型；barrier 本身与资源跨 frame 生命周期无关。
    // ------------------------------------------------------------------------
    enum class PipelineStage : uint32_t
    {
        None = 0,
        VertexInput = 1u << 0,
        VertexShader = 1u << 1,
        FragmentShader = 1u << 2,
        ComputeShader = 1u << 3,
        ColorAttachment = 1u << 4,
        DepthAttachment = 1u << 5,
        Transfer = 1u << 6,
        AllGraphics = 1u << 7,
        AllCommands = 1u << 8,
        // 光追：加速结构构建阶段。用于表达
        // 「AS build 写 → shader（rayQuery）读」依赖。VK 后端映射为
        // VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR。
        AccelerationStructureBuild = 1u << 9,
    };

    inline constexpr PipelineStage operator|(PipelineStage a, PipelineStage b)
    {
        return static_cast<PipelineStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline constexpr bool HasFlag(PipelineStage v, PipelineStage f)
    {
        return (static_cast<uint32_t>(v) & static_cast<uint32_t>(f)) != 0;
    }

    enum class AccessFlags : uint32_t
    {
        None = 0,
        ShaderRead = 1u << 0,
        ShaderWrite = 1u << 1,
        ColorAttachmentRead = 1u << 2,
        ColorAttachmentWrite = 1u << 3,
        DepthRead = 1u << 4,
        DepthWrite = 1u << 5,
        TransferRead = 1u << 6,
        TransferWrite = 1u << 7,
        // 光追：加速结构读写访问。VK 后端映射为
        // VK_ACCESS_2_ACCELERATION_STRUCTURE_READ/WRITE_BIT_KHR。
        AccelerationStructureRead = 1u << 8,
        AccelerationStructureWrite = 1u << 9,
    };

    inline constexpr AccessFlags operator|(AccessFlags a, AccessFlags b)
    {
        return static_cast<AccessFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline constexpr bool HasFlag(AccessFlags v, AccessFlags f)
    {
        return (static_cast<uint32_t>(v) & static_cast<uint32_t>(f)) != 0;
    }

    // 单张纹理的 layout 转换描述（仅 VK 后端使用；GL 忕略）
    enum class TextureLayout : uint8_t
    {
        Undefined = 0,
        General, // VK_IMAGE_LAYOUT_GENERAL（storage image 读写）
        ShaderReadOnly, // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ColorAttachment, // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        DepthStencilAttachment, // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        TransferSrc,
        TransferDst,
        PresentSrc,
    };

    struct TextureBarrier
    {
        TextureHandle texture;
        TextureLayout oldLayout = TextureLayout::Undefined;
        TextureLayout newLayout = TextureLayout::General;
        AccessFlags srcAccess = AccessFlags::None;
        AccessFlags dstAccess = AccessFlags::None;
    };

    struct PipelineBarrierDesc
    {
        PipelineStage srcStage = PipelineStage::None;
        PipelineStage dstStage = PipelineStage::None;
        AccessFlags srcGlobalAccess = AccessFlags::None;
        AccessFlags dstGlobalAccess = AccessFlags::None;
        std::vector<TextureBarrier> textureBarriers;
    };
} // namespace TitusRHI
