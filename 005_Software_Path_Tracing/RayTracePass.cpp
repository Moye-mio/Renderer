// ============================================================================
// 005_Software_Path_Tracing - RayTracePass.cpp
//
// Trace（解析求交 + 着色 → 累积双缓冲）→ Display（曝光 / 色调映射 → backbuffer）。
// ============================================================================
#include "RayTracePass.h"
#include "CornellBoxScene.h"
#include "RayTracingContext.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "FileSystem.h"
#include "Logger.h"
#include "TracySupport.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

namespace
{
    // 累积缓冲要存成百上千个样本的均值，16F 的 10 位尾数在几百样本后就开始
    // 吃不住增量（新样本权重 1/N 太小会被直接舍掉，画面停在半收敛状态），
    // 所以这里用 32F。
    constexpr TitusRHI::Format kAccumFormat = TitusRHI::Format::R32G32B32A32_SFLOAT;

    // shader 侧的循环上界，和 RayTrace_FS.glsl 的 kMax* 常量保持一致。
    constexpr int kMaxSamplesPerFrame = 64;
    constexpr int kMaxBounces         = 64;
    constexpr int kMaxShadowSamples   = 64;

    // std140 布局，与 RayTrace_FS.glsl 的 u_Frame 块逐字段对应。
    struct FrameBlock
    {
        TitusMath::Vec4 cameraPos{};
        TitusMath::Vec4 cameraRight{};   // w: tan(fovY/2) * aspect
        TitusMath::Vec4 cameraUp{};      // w: tan(fovY/2)
        TitusMath::Vec4 cameraForward{};
        TitusMath::Vec4 resolution{};    // xy: 分辨率, zw: 1 / 分辨率
        TitusMath::Vec4 sampling{};      // x: N, y: 本帧样本数, z: 最大弹射, w: 帧种子
        TitusMath::Vec4 options{};       // x: 模式, y: NEE, z: AO 半径, w: 阴影 / AO 采样数
    };
    static_assert(sizeof(FrameBlock) == 112, "FrameBlock std140 size");

    struct DisplayBlock
    {
        TitusMath::Vec4 params{}; // x: 曝光, y: 色调映射, z: gamma, w: 直通开关
    };
    static_assert(sizeof(DisplayBlock) == 16, "DisplayBlock std140 size");

    static_assert(sizeof(CornellBoxScene::SceneBlock) == 128, "SceneBlock std140 size");

    bool SameMatrix(const TitusMath::Mat4& a, const TitusMath::Mat4& b)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (a.m[i] != b.m[i])
                return false;
        }
        return true;
    }

    bool LoadShaderBytes(const std::string& path, std::vector<uint8_t>& out)
    {
        return TitusAsset::ReadAllBytes(path, out);
    }
}

RayTracePass::RayTracePass()
{
    passEvent = TitusRHI::ERenderPassEvent::OpaqueShading;
}

void RayTracePass::Init(TitusRHI::IGDevice& device)
{
    using namespace TitusRHI;

    const std::string shaderDir = std::string(SOLUTION_DIR) + "005_Software_Path_Tracing/Shader/";

    std::vector<uint8_t> vsBytes;
    if (!LoadShaderBytes(shaderDir + "Blit_VS.glsl", vsBytes))
    {
        LOG_STREAM_ERROR("RayTracePass") << "missing Blit_VS.glsl; pipelines not created";
        return;
    }

    ShaderDesc vsDesc{};
    vsDesc.stage = ShaderStage::Vertex;
    vsDesc.code = vsBytes.data();
    vsDesc.bytes = vsBytes.size();
    vsDesc.entryPoint = "main";
    vsDesc.debugName = "RayTracePass.BlitVS";
    m_blitVS = device.CreateShader(vsDesc);

    // ---- Trace 管线：写 RGBA32F 累积缓冲，读上一张累积缓冲 ----
    {
        std::vector<uint8_t> fsBytes;
        if (LoadShaderBytes(shaderDir + "RayTrace_FS.glsl", fsBytes))
        {
            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "RayTracePass.TraceFS";
            m_traceFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_blitVS;
            pd.fragmentShader = m_traceFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.depthStencil.depthTestEnable = false;
            pd.depthStencil.depthWriteEnable = false;
            pd.blend.attachments.resize(1);
            pd.rtLayout.colorFormats = {kAccumFormat};

            ResourceBinding rbHistory{};
            rbHistory.name = "u_History";
            rbHistory.set = 0;
            rbHistory.binding = 0;
            rbHistory.type = ResourceBindingType::CombinedImageSampler;
            rbHistory.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbHistory);

            ResourceBinding rbScene{};
            rbScene.name = "u_Scene";
            rbScene.set = 0;
            rbScene.binding = 1;
            rbScene.type = ResourceBindingType::UniformBuffer;
            rbScene.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbScene);

            ResourceBinding rbFrame{};
            rbFrame.name = "u_Frame";
            rbFrame.set = 0;
            rbFrame.binding = 2;
            rbFrame.type = ResourceBindingType::UniformBuffer;
            rbFrame.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbFrame);

            pd.debugName = "RayTracePass.TracePipeline";
            m_tracePipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("RayTracePass") << "missing RayTrace_FS.glsl";
        }
    }

    // ---- Display 管线：累积均值 → backbuffer ----
    {
        std::vector<uint8_t> fsBytes;
        if (LoadShaderBytes(shaderDir + "Display_FS.glsl", fsBytes))
        {
            ShaderDesc fsDesc{};
            fsDesc.stage = ShaderStage::Fragment;
            fsDesc.code = fsBytes.data();
            fsDesc.bytes = fsBytes.size();
            fsDesc.entryPoint = "main";
            fsDesc.debugName = "RayTracePass.DisplayFS";
            m_displayFS = device.CreateShader(fsDesc);

            GraphicsPipelineDesc pd{};
            pd.vertexShader = m_blitVS;
            pd.fragmentShader = m_displayFS;
            pd.topology = PrimitiveTopology::TriangleList;
            pd.rasterizer.cullMode = CullMode::None;
            pd.depthStencil.depthTestEnable = false;
            pd.depthStencil.depthWriteEnable = false;
            pd.blend.attachments.resize(1);

            ResourceBinding rbColor{};
            rbColor.name = "u_Accumulation";
            rbColor.set = 0;
            rbColor.binding = 0;
            rbColor.type = ResourceBindingType::CombinedImageSampler;
            rbColor.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbColor);

            ResourceBinding rbParams{};
            rbParams.name = "u_Display";
            rbParams.set = 0;
            rbParams.binding = 1;
            rbParams.type = ResourceBindingType::UniformBuffer;
            rbParams.stages = ShaderStage::Fragment;
            pd.resourceBindings.push_back(rbParams);

            pd.debugName = "RayTracePass.DisplayPipeline";
            m_displayPipeline = device.CreatePipeline(pd);
        }
        else
        {
            LOG_STREAM_ERROR("RayTracePass") << "missing Display_FS.glsl";
        }
    }

    {
        // 累积缓冲和 backbuffer 同分辨率，1:1 取点即可，不需要线性过滤。
        SamplerDesc sd{};
        sd.minFilter = FilterMode::Nearest;
        sd.magFilter = FilterMode::Nearest;
        sd.mipmapMode = MipmapMode::Nearest;
        sd.addressU = sd.addressV = sd.addressW = AddressMode::ClampToEdge;
        sd.debugName = "RayTracePass.PointSampler";
        m_pointSampler = device.CreateSampler(sd);
    }

    {
        BufferDesc bd{};
        bd.size = sizeof(CornellBoxScene::SceneBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "RayTracePass.UBO.Scene";
        m_sceneUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(FrameBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "RayTracePass.UBO.Frame";
        m_frameUbo = device.CreateBuffer(bd);
    }
    {
        BufferDesc bd{};
        bd.size = sizeof(DisplayBlock);
        bd.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.debugName = "RayTracePass.UBO.Display";
        m_displayUbo = device.CreateBuffer(bd);
    }

    uint32_t w = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    uint32_t h = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    if (w == 0) w = 1280;
    if (h == 0) h = 1280;
    EnsureTargets(device, w, h);
}

void RayTracePass::DestroyTargets(TitusRHI::IGDevice& device)
{
    for (int i = 0; i < 2; ++i)
    {
        if (m_accumRT[i].IsValid()) device.Destroy(m_accumRT[i]);
        if (m_accum[i].IsValid()) device.Destroy(m_accum[i]);
        m_accumRT[i] = {};
        m_accum[i] = {};
    }
}

void RayTracePass::EnsureTargets(TitusRHI::IGDevice& device, uint32_t width, uint32_t height)
{
    using namespace TitusRHI;

    if (width == 0 || height == 0)
        return;
    if (m_width == width && m_height == height
        && m_accumRT[0].IsValid() && m_accumRT[1].IsValid())
        return;

    device.WaitIdle();
    DestroyTargets(device);

    for (int i = 0; i < 2; ++i)
    {
        TextureDesc td{};
        td.type = TextureType::Tex2D;
        td.format = kAccumFormat;
        td.width = width;
        td.height = height;
        td.mipLevels = 1;
        td.samples = 1;
        td.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        td.debugName = (i == 0) ? "RayTracePass.Accum0" : "RayTracePass.Accum1";
        m_accum[i] = device.CreateTexture(td);

        RenderTargetDesc rt{};
        rt.width = width;
        rt.height = height;
        rt.colorAttachments.push_back({m_accum[i], 0, 0});
        rt.debugName = (i == 0) ? "RayTracePass.AccumRT0" : "RayTracePass.AccumRT1";
        m_accumRT[i] = device.CreateRenderTarget(rt);
    }

    m_width = width;
    m_height = height;
    m_accumWrite = 0;
    m_accumLatest = 0;
    m_accumulatedSamples = 0;
    m_needsClear = true;

    LOG_STREAM_INFO("RayTracePass") << "accumulation targets " << width << "x" << height;
}

void RayTracePass::Destroy(TitusRHI::IGDevice& device)
{
    DestroyTargets(device);
    if (m_displayUbo.IsValid()) device.Destroy(m_displayUbo);
    if (m_frameUbo.IsValid()) device.Destroy(m_frameUbo);
    if (m_sceneUbo.IsValid()) device.Destroy(m_sceneUbo);
    if (m_pointSampler.IsValid()) device.Destroy(m_pointSampler);
    if (m_displayPipeline.IsValid()) device.Destroy(m_displayPipeline);
    if (m_displayFS.IsValid()) device.Destroy(m_displayFS);
    if (m_tracePipeline.IsValid()) device.Destroy(m_tracePipeline);
    if (m_traceFS.IsValid()) device.Destroy(m_traceFS);
    if (m_blitVS.IsValid()) device.Destroy(m_blitVS);
    m_displayUbo = {};
    m_frameUbo = {};
    m_sceneUbo = {};
    m_pointSampler = {};
    m_displayPipeline = {};
    m_displayFS = {};
    m_tracePipeline = {};
    m_traceFS = {};
    m_blitVS = {};
    m_width = 0;
    m_height = 0;
    m_accumulatedSamples = 0;
    m_hasPrevCamera = false;
}

bool RayTracePass::ShouldResetAccumulation(const TitusMath::Mat4& view,
                                           const TitusMath::Mat4& proj) const
{
    if (!m_hasPrevCamera)
        return true;
    return !SameMatrix(view, m_prevView) || !SameMatrix(proj, m_prevProj);
}

void RayTracePass::Record(TitusRHI::IGDevice& device,
                          TitusRHI::RenderCommandList& cmd,
                          uint32_t /*frameIndex*/,
                          uint32_t /*imageIndex*/)
{
    using namespace TitusRHI;

    if (!m_ctx || !m_scene)
        return;
    if (!m_tracePipeline.IsValid() || !m_displayPipeline.IsValid())
        return;

    const uint32_t vpW = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowWidth());
    const uint32_t vpH = static_cast<uint32_t>(WINDOW_KEYWORD::GetWindowHeight());
    const bool resized = (vpW != m_width || vpH != m_height);

    EnsureTargets(device, vpW, vpH);
    if (!m_accumRT[0].IsValid() || !m_accumRT[1].IsValid())
        return;

    // 纹理刚建好时内容未定义、VK 里布局还是 UNDEFINED。把两张都清成 0：
    // 既满足绑成采样源时的布局要求，也让"首帧不读历史"退化成纯优化，
    // 而不是正确性的前提。
    if (m_needsClear)
    {
        ZoneScopedN("RayTrace::ClearAccum");
        for (int i = 0; i < 2; ++i)
        {
            RenderPassBeginInfo rp{};
            rp.renderTarget = m_accumRT[i];
            RenderPassAttachmentOp colorOp{};
            colorOp.loadOp = LoadOp::Clear;
            colorOp.storeOp = StoreOp::Store;
            colorOp.clearValue.color[0] = 0.0f;
            colorOp.clearValue.color[1] = 0.0f;
            colorOp.clearValue.color[2] = 0.0f;
            colorOp.clearValue.color[3] = 1.0f;
            rp.colorOps.push_back(colorOp);
            rp.hasDepthStencil = false;
            cmd.BeginRenderPass(rp);
            cmd.EndRenderPass();
        }
        m_needsClear = false;
    }

    const TitusMath::Mat4 view = CAMERA::GetMainCameraViewMatrix();
    const TitusMath::Mat4 proj = CAMERA::GetMainCameraProjectionMatrix();

    if (resized || m_ctx->accumDirty || ShouldResetAccumulation(view, proj))
    {
        m_accumulatedSamples = 0;
        m_ctx->accumDirty = false;
    }
    m_prevView = view;
    m_prevProj = proj;
    m_hasPrevCamera = true;

    // 到了采样上限就整趟跳过 Trace：这时画面已经收敛，再算也只是把同一个
    // 均值重算一遍，白烧 GPU（同时也让截图前的画面完全稳定）。
    const uint32_t maxSamples = (m_ctx->maxAccumSamples > 0)
        ? static_cast<uint32_t>(m_ctx->maxAccumSamples) : 0u;
    const bool converged = (maxSamples > 0 && m_accumulatedSamples >= maxSamples);

    if (!converged)
    {
        const uint32_t writeIdx = m_accumWrite;
        const uint32_t readIdx = 1u - m_accumWrite;
        const int samplesThisFrame =
            std::clamp(m_ctx->samplesPerFrame, 1, kMaxSamplesPerFrame);

        if (m_sceneUbo.IsValid())
        {
            ZoneScopedN("RayTrace::UpdateScene");
            const CornellBoxScene::SceneBlock data = m_scene->Pack();
            device.UpdateBuffer(m_sceneUbo, &data, sizeof(data), 0);
        }

        if (m_frameUbo.IsValid())
        {
            ZoneScopedN("RayTrace::UpdateFrame");

            // view = R * T，R 的三行分别是世界空间的 right / up / -forward。
            const TitusMath::Vec3 right(view[0][0], view[1][0], view[2][0]);
            const TitusMath::Vec3 up(view[0][1], view[1][1], view[2][1]);
            const TitusMath::Vec3 forward =
                -TitusMath::Vec3(view[0][2], view[1][2], view[2][2]);

            // 视锥半角从投影矩阵反解，省得和 FlyCameraConfig 的 fov / aspect
            // 各维护一份，改了一处忘了另一处就会错位。
            const float tanHalfX = (proj[0][0] != 0.0f) ? 1.0f / proj[0][0] : 1.0f;
            const float tanHalfY = (proj[1][1] != 0.0f) ? 1.0f / proj[1][1] : 1.0f;

            const bool isAo = (m_ctx->mode == RTTechnique::AmbientOcclusion);
            const int shadowSamples = std::clamp(
                isAo ? m_ctx->aoSamples : m_ctx->lightSamples, 1, kMaxShadowSamples);

            FrameBlock fb{};
            fb.cameraPos = TitusMath::Vec4(CAMERA::GetMainCameraPosition(), 0.0f);
            fb.cameraRight = TitusMath::Vec4(right, tanHalfX);
            fb.cameraUp = TitusMath::Vec4(up, tanHalfY);
            fb.cameraForward = TitusMath::Vec4(forward, 0.0f);
            fb.resolution = TitusMath::Vec4(
                static_cast<float>(m_width),
                static_cast<float>(m_height),
                1.0f / static_cast<float>(m_width),
                1.0f / static_cast<float>(m_height));
            fb.sampling = TitusMath::Vec4(
                static_cast<float>(m_accumulatedSamples),
                static_cast<float>(samplesThisFrame),
                static_cast<float>(std::clamp(m_ctx->maxBounces, 1, kMaxBounces)),
                static_cast<float>(m_frameSeed));
            fb.options = TitusMath::Vec4(
                static_cast<float>(static_cast<int>(m_ctx->mode)),
                m_ctx->enableNee ? 1.0f : 0.0f,
                m_ctx->aoRadius,
                static_cast<float>(shadowSamples));
            device.UpdateBuffer(m_frameUbo, &fb, sizeof(fb), 0);
        }

        {
            ZoneScopedN("RayTrace::Trace");

            RenderPassBeginInfo rp{};
            rp.renderTarget = m_accumRT[writeIdx];
            RenderPassAttachmentOp colorOp{};
            // 全屏三角形覆盖每个像素，不必先清。
            colorOp.loadOp = LoadOp::DontCare;
            colorOp.storeOp = StoreOp::Store;
            rp.colorOps.push_back(colorOp);
            rp.hasDepthStencil = false;

            cmd.BeginRenderPass(rp);

            Viewport vp{};
            vp.width = static_cast<float>(m_width);
            vp.height = static_cast<float>(m_height);
            cmd.SetViewport(vp);
            Rect2D sc{};
            sc.width = m_width;
            sc.height = m_height;
            cmd.SetScissor(sc);

            cmd.BindPipeline(m_tracePipeline);

            {
                ResourceSetDesc rs{};

                ResourceBindingValue history{};
                history.binding = 0;
                history.type = ResourceBindingType::CombinedImageSampler;
                history.texture = m_accum[readIdx];
                history.sampler = m_pointSampler;
                rs.bindings.push_back(history);

                ResourceBindingValue scene{};
                scene.binding = 1;
                scene.type = ResourceBindingType::UniformBuffer;
                scene.buffer = m_sceneUbo;
                scene.bufferOffset = 0;
                scene.bufferRange = sizeof(CornellBoxScene::SceneBlock);
                rs.bindings.push_back(scene);

                ResourceBindingValue frame{};
                frame.binding = 2;
                frame.type = ResourceBindingType::UniformBuffer;
                frame.buffer = m_frameUbo;
                frame.bufferOffset = 0;
                frame.bufferRange = sizeof(FrameBlock);
                rs.bindings.push_back(frame);

                cmd.BindResourceSet(0, rs);
            }

            cmd.Draw(3);
            cmd.EndRenderPass();
        }

        m_accumulatedSamples += static_cast<uint32_t>(samplesThisFrame);
        m_accumLatest = writeIdx;
        m_accumWrite = readIdx;
        // 帧种子只要在一轮累积内不重复即可；掩到 16 位是为了让 float 能精确
        // 表示它（float 只能精确表示到 2^24，超了之后相邻帧会退化成同一个种子）。
        m_frameSeed = (m_frameSeed + 1u) & 0xFFFFu;
    }

    m_ctx->accumulatedSamples = m_accumulatedSamples;

    if (m_displayUbo.IsValid())
    {
        ZoneScopedN("RayTrace::UpdateDisplay");

        // 法线可视化存的是编码方向，不是线性辐射亮度，得跳过整条显示变换。
        const bool passthrough = (m_ctx->mode == RTTechnique::Normal);

        DisplayBlock data{};
        data.params = TitusMath::Vec4(
            m_ctx->exposure,
            static_cast<float>(static_cast<int>(m_ctx->toneMap)),
            2.2f,
            passthrough ? 1.0f : 0.0f);
        device.UpdateBuffer(m_displayUbo, &data, sizeof(data), 0);
    }

    {
        ZoneScopedN("RayTrace::Display");

        RenderPassBeginInfo rp{};
        RenderPassAttachmentOp colorOp{};
        colorOp.loadOp = LoadOp::Clear;
        colorOp.storeOp = StoreOp::Store;
        colorOp.clearValue.color[0] = 0.0f;
        colorOp.clearValue.color[1] = 0.0f;
        colorOp.clearValue.color[2] = 0.0f;
        colorOp.clearValue.color[3] = 1.0f;
        rp.colorOps.push_back(colorOp);
        rp.hasDepthStencil = true;
        rp.depthStencilOp.loadOp = LoadOp::Clear;
        rp.depthStencilOp.storeOp = StoreOp::DontCare;
        rp.depthStencilOp.clearValue.depth = 1.0f;
        rp.depthStencilOp.clearValue.stencil = 0;

        cmd.BeginRenderPass(rp);

        Viewport vp{};
        vp.width = static_cast<float>(vpW);
        vp.height = static_cast<float>(vpH);
        cmd.SetViewport(vp);
        Rect2D sc{};
        sc.width = vpW;
        sc.height = vpH;
        cmd.SetScissor(sc);

        cmd.BindPipeline(m_displayPipeline);

        {
            ResourceSetDesc rs{};

            ResourceBindingValue tex{};
            tex.binding = 0;
            tex.type = ResourceBindingType::CombinedImageSampler;
            tex.texture = m_accum[m_accumLatest];
            tex.sampler = m_pointSampler;
            rs.bindings.push_back(tex);

            ResourceBindingValue params{};
            params.binding = 1;
            params.type = ResourceBindingType::UniformBuffer;
            params.buffer = m_displayUbo;
            params.bufferOffset = 0;
            params.bufferRange = sizeof(DisplayBlock);
            rs.bindings.push_back(params);

            cmd.BindResourceSet(0, rs);
        }

        cmd.Draw(3);
        cmd.EndRenderPass();
    }
}
