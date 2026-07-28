// ============================================================================
// 010_UnifiedTriangle - TrianglePass.cpp
// 业务侧 Pass 实现：完全只依赖 RendererCore 接口；通过 IGDevice::GetBackend()
// 选择不同 shader 资源（VK 用 .spv；GL 用 .glsl），其余逻辑两端共享。
// ============================================================================
#include "TrianglePass.h"

#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"

// SOLUTION_DIR 由 vcxproj 注入，指向仓库根目录
#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

TrianglePass::TrianglePass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void TrianglePass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;
    const GBackend be = device.GetBackend();

    // 1) Shader 资源选择：VK → .spv；GL → .glsl
    const std::string baseDir = std::string(SOLUTION_DIR) + "Examples/Test_000_UnifiedTriangle/Shader/";
    std::string vsPath, fsPath;
    if (be == GBackend::Vulkan)
    {
        vsPath = baseDir + "triangle.vert.spv";
        fsPath = baseDir + "triangle.frag.spv";
    }
    else
    {
        vsPath = baseDir + "triangle.vert.glsl";
        fsPath = baseDir + "triangle.frag.glsl";
    }

    std::vector<uint8_t> vsBytes, fsBytes;
    if (!TitusAsset::ReadAllBytes(vsPath, vsBytes) ||
        !TitusAsset::ReadAllBytes(fsPath, fsBytes))
    {
        LOG_STREAM_ERROR("TrianglePass") << "shader files missing";
        return;
    }

    ShaderDesc vsDesc{};
    vsDesc.stage      = ShaderStage::Vertex;
    vsDesc.code       = vsBytes.data();
    vsDesc.bytes      = vsBytes.size();
    vsDesc.entryPoint = "main";
    vsDesc.debugName  = "TrianglePass.VS";

    ShaderDesc fsDesc{};
    fsDesc.stage      = ShaderStage::Fragment;
    fsDesc.code       = fsBytes.data();
    fsDesc.bytes      = fsBytes.size();
    fsDesc.entryPoint = "main";
    fsDesc.debugName  = "TrianglePass.FS";

    mVS = device.CreateShader(vsDesc);
    mFS = device.CreateShader(fsDesc);

    // 2) Pipeline：顶点由 Shader 内硬编码，无需 VertexInput；关闭深度/剔除
    GraphicsPipelineDesc desc{};
    desc.vertexShader   = mVS;
    desc.fragmentShader = mFS;
    desc.topology       = PrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode    = CullMode::None;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rtLayout.colorFormats = { Format::B8G8R8A8_UNORM };
    desc.debugName = "TrianglePass.Pipeline";

    mPipeline = device.CreatePipeline(desc);
}

void TrianglePass::Destroy(TitusRHI::IGDevice& device)
{
    if (mPipeline.IsValid()) device.Destroy(mPipeline);
    if (mFS.IsValid())       device.Destroy(mFS);
    if (mVS.IsValid())       device.Destroy(mVS);
    mPipeline = {};
    mFS = {};
    mVS = {};
}

void TrianglePass::Update(TitusRHI::IGDevice& /*device*/, uint32_t /*frameIndex*/)
{
    // 三角形 Pass 没有 per-frame UBO，留空
}

void TrianglePass::Record(TitusRHI::IGDevice&        /*device*/,
                          TitusRHI::RenderCommandList& cmd,
                          uint32_t                       /*frameIndex*/,
                          uint32_t                       /*imageIndex*/)
{
    using namespace TitusRHI;

    // 用默认 backbuffer 启动 RenderPass，清屏深色靛蓝
    RenderPassBeginInfo rp{};
    RenderPassAttachmentOp colorOp{};
    colorOp.loadOp  = LoadOp::Clear;
    colorOp.storeOp = StoreOp::Store;
    colorOp.clearValue.color[0] = 0.05f;
    colorOp.clearValue.color[1] = 0.05f;
    colorOp.clearValue.color[2] = 0.08f;
    colorOp.clearValue.color[3] = 1.0f;
    rp.colorOps.push_back(colorOp);
    rp.hasDepthStencil = false;

    cmd.BeginRenderPass(rp);

    // 视口 / 裁剪：1280 x 720（与 main 中创建窗口尺寸一致；后续可改为查询 device）
    Viewport vp{};
    vp.width  = 1280.0f;
    vp.height = 720.0f;
    cmd.SetViewport(vp);

    Rect2D sc{};
    sc.width  = 1280;
    sc.height = 720;
    cmd.SetScissor(sc);

    cmd.BindPipeline(mPipeline);
    cmd.Draw(3);

    cmd.EndRenderPass();
}
