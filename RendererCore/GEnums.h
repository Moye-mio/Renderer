#pragma once
// ============================================================================
// RendererCore - GEnums
// 后端无关的纯枚举集合：所有枚举值均不映射到任何具体后端值（GLenum / VkXxx）。
// 后端各自维护 Translate 表把这里的枚举翻译为原生值。
// 设计参考：RendererCore 设计方案 §3.4。
// ============================================================================
#include <cstdint>

namespace TitusRHI
{
    // ------------------------------------------------------------------------
    // 后端类型 —— 用于运行期能力查询与工厂选择
    // ------------------------------------------------------------------------
    enum class GBackend : uint8_t
    {
        Unknown = 0,
        OpenGL,
        Vulkan,
        Null,        // 用于 headless / 单元测试（无 GPU、无窗口）
        // 预留：D3D12, Metal ...
    };

    // ------------------------------------------------------------------------
    // 像素 / 顶点 / 索引 格式
    // ------------------------------------------------------------------------
    enum class Format : uint16_t
    {
        Undefined = 0,

        // 8-bit
        R8_UNORM,
        R8G8_UNORM,
        R8G8B8_UNORM,
        R8G8B8A8_UNORM,
        R8G8B8A8_SRGB,
        B8G8R8A8_UNORM,
        B8G8R8A8_SRGB,

        // 16-bit float
        R16_SFLOAT,
        R16G16_SFLOAT,
        R16G16B16A16_SFLOAT,

        // 32-bit float
        R32_SFLOAT,
        R32G32_SFLOAT,
        R32G32B32_SFLOAT,
        R32G32B32A32_SFLOAT,

        // 32-bit int
        R32_UINT,
        R32_SINT,

        // depth / stencil
        D16_UNORM,
        D24_UNORM_S8_UINT,
        D32_SFLOAT,
        D32_SFLOAT_S8_UINT,
    };

    // ------------------------------------------------------------------------
    // 图元拓扑
    // ------------------------------------------------------------------------
    enum class PrimitiveTopology : uint8_t
    {
        PointList = 0,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
    };

    // ------------------------------------------------------------------------
    // 索引类型
    // ------------------------------------------------------------------------
    enum class IndexType : uint8_t
    {
        UInt16 = 0,
        UInt32,
    };

    // ------------------------------------------------------------------------
    // 光栅化
    // ------------------------------------------------------------------------
    enum class CullMode : uint8_t
    {
        None = 0,
        Front,
        Back,
        FrontAndBack,
    };

    enum class FrontFace : uint8_t
    {
        CounterClockwise = 0,
        Clockwise,
    };

    enum class PolygonMode : uint8_t
    {
        Fill = 0,
        Line,
        Point,
    };

    // ------------------------------------------------------------------------
    // 深度 / 模板 比较函数
    // ------------------------------------------------------------------------
    enum class CompareOp : uint8_t
    {
        Never = 0,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    // ------------------------------------------------------------------------
    // 混合
    // ------------------------------------------------------------------------
    enum class BlendFactor : uint8_t
    {
        Zero = 0,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        ConstantColor,
        OneMinusConstantColor,
    };

    enum class BlendOp : uint8_t
    {
        Add = 0,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    // ------------------------------------------------------------------------
    // 附件 LoadOp / StoreOp（Vulkan 风格语义；GL 后端用 glClear / glInvalidateFramebuffer 模拟）
    // ------------------------------------------------------------------------
    enum class LoadOp : uint8_t
    {
        Load = 0,
        Clear,
        DontCare,
    };

    enum class StoreOp : uint8_t
    {
        Store = 0,
        DontCare,
    };

    // ------------------------------------------------------------------------
    // 着色器阶段（位标志，可按位或）
    // ------------------------------------------------------------------------
    enum class ShaderStage : uint32_t
    {
        None = 0,
        Vertex = 1u << 0,
        Fragment = 1u << 1,
        Geometry = 1u << 2,
        Compute = 1u << 3,
        TessControl = 1u << 4,
        TessEvaluation = 1u << 5,
        // 光追管线阶段（任务 12 / 需求 3.3，P1 路线 B）：仅追加，位标志语义不变。
        RayGen = 1u << 6,
        Miss = 1u << 7,
        ClosestHit = 1u << 8,
        AnyHit = 1u << 9,
        Intersection = 1u << 10,
        Callable = 1u << 11,
    };

    inline constexpr ShaderStage operator|(ShaderStage a, ShaderStage b)
    {
        return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline constexpr ShaderStage operator&(ShaderStage a, ShaderStage b)
    {
        return static_cast<ShaderStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    // ------------------------------------------------------------------------
    // Buffer 用途（位标志，可按位或）
    // ------------------------------------------------------------------------
    enum class BufferUsage : uint32_t
    {
        None = 0,
        VertexBuffer = 1u << 0,
        IndexBuffer = 1u << 1,
        UniformBuffer = 1u << 2,
        StorageBuffer = 1u << 3,
        TransferSrc = 1u << 4,
        TransferDst = 1u << 5,
        Indirect = 1u << 6,
        // 光追（任务 2 / 需求 3）：仅追加，不改动上方既有位值。
        ShaderDeviceAddress = 1u << 7,              // 可取设备地址（BDA），AS 输入/scratch 必需
        AccelerationStructureStorage = 1u << 8,     // AS backing buffer
        AccelerationStructureBuildInput = 1u << 9,  // BLAS/TLAS 几何/instance 输入（只读）
        ShaderBindingTable = 1u << 10,              // SBT buffer（P1 路线 B 使用）
    };

    inline constexpr BufferUsage operator|(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline constexpr BufferUsage operator&(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline constexpr bool HasFlag(BufferUsage v, BufferUsage f)
    {
        return (static_cast<uint32_t>(v) & static_cast<uint32_t>(f)) != 0;
    }

    // ------------------------------------------------------------------------
    // Texture 用途（位标志）
    // ------------------------------------------------------------------------
    enum class TextureUsage : uint32_t
    {
        None = 0,
        Sampled = 1u << 0,
        Storage = 1u << 1,
        ColorAttachment = 1u << 2,
        DepthStencilAttachment = 1u << 3,
        TransferSrc = 1u << 4,
        TransferDst = 1u << 5,
    };

    inline constexpr TextureUsage operator|(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline constexpr TextureUsage operator&(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline constexpr bool HasFlag(TextureUsage v, TextureUsage f)
    {
        return (static_cast<uint32_t>(v) & static_cast<uint32_t>(f)) != 0;
    }

    // ------------------------------------------------------------------------
    // 内存使用方式（语义统一；后端各自映射）
    // ------------------------------------------------------------------------
    enum class MemoryUsage : uint8_t
    {
        GpuOnly = 0, // 仅 GPU 访问（DEVICE_LOCAL / static draw）
        CpuToGpu, // CPU 写、GPU 读（HOST_VISIBLE | HOST_COHERENT / dynamic draw）
        GpuToCpu, // GPU 写、CPU 读（回读）
        CpuOnly, // CPU 暂存
    };

    // ------------------------------------------------------------------------
    // 纹理类型 / 寻址 / 过滤
    // ------------------------------------------------------------------------
    enum class TextureType : uint8_t
    {
        Tex1D = 0,
        Tex2D,
        Tex3D,
        TexCube,
        Tex2DArray,
    };

    enum class FilterMode : uint8_t
    {
        Nearest = 0,
        Linear,
    };

    enum class MipmapMode : uint8_t
    {
        Nearest = 0,
        Linear,
    };

    enum class AddressMode : uint8_t
    {
        Repeat = 0,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
    };
}
