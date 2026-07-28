#pragma once
// ============================================================================
// RendererCore - DeviceLifecycleTest
// 跨后端"设备生命周期 + 资源生命周期 + 帧循环 + 延迟销毁"单元测试。
// 默认基于 GDeviceHeadless，运行不依赖任何 GPU/窗口，可在 CI / 本地任意调用。
//
// 目标覆盖：
//   - Init -> Shutdown 流程能跑通；
//   - CreateBuffer/Texture/Sampler/Shader/Pipeline 句柄非空；
//   - SamplerCache / PipelineCache 命中（同 Desc 第二次拿到同句柄）；
//   - FindBuffer / FindTexture / FindShader 元数据可查；
//   - BeginFrame / Submit / Present 三件套不崩；
//   - Destroy 之后 Find* 返回 nullptr；
//   - 延迟销毁队列在足够帧后被释放（FlushAllPendingDestroys 兜底）。
//
// 设计参考：requirements.md 13.x；task-item.md M4-10。
// ============================================================================
#include <cstdio>
#include <cstdint>

#include "Logger.h"
#include "GDeviceHeadless.h"
#include "GDescs.h"
#include "GHandle.h"
#include "IWindow.h"
#include "RenderCommandList.h"

namespace TitusRHI::Tests
{
    // 简易断言：失败则打印并累计错误数，不立即终止，方便看到所有失败点
    struct TestRecorder
    {
        int failures = 0;

        void Check(bool cond, const char* what, const char* file, int line)
        {
            if (!cond)
            {
                ++failures;
                LOG_ERROR("DeviceLifecycleTest",
                          "FAIL %s  (%s:%d)",
                          what, file, line);
            }
        }
    };

#define DLT_CHECK(rec, cond) (rec).Check((cond), #cond, __FILE__, __LINE__)

    inline int RunDeviceLifecycleTests()
    {
        LOG_INFO("DeviceLifecycleTest", "begin");

        TestRecorder rec;
        GDeviceHeadless device;

        // ----------------------------------------------------------------
        // 1) Init 流程
        // ----------------------------------------------------------------
        GDeviceDesc desc{};
        desc.framesInFlight = 2;
        const bool inited = device.Init(desc, /*window*/nullptr);
        DLT_CHECK(rec, inited);
        DLT_CHECK(rec, device.GetBackend() == GBackend::Null);
        DLT_CHECK(rec, device.GetCaps().maxTextureSize2D == 4096);

        // ----------------------------------------------------------------
        // 2) 资源创建：句柄非空
        // ----------------------------------------------------------------
        BufferDesc bdesc{};
        bdesc.size = 1024;
        bdesc.usage = BufferUsage::VertexBuffer;
        bdesc.memory = MemoryUsage::GpuOnly;
        BufferHandle vb = device.CreateBuffer(bdesc);
        DLT_CHECK(rec, vb.id != 0);

        TextureDesc tdesc{};
        tdesc.width = 64;
        tdesc.height = 64;
        tdesc.format = Format::R8G8B8A8_UNORM;
        tdesc.usage = TextureUsage::Sampled;
        TextureHandle tex = device.CreateTexture(tdesc);
        DLT_CHECK(rec, tex.id != 0);

        ShaderDesc sdesc{};
        sdesc.stage = ShaderStage::Vertex;
        sdesc.entryPoint = "main";
        ShaderHandle sh = device.CreateShader(sdesc);
        DLT_CHECK(rec, sh.id != 0);

        // ----------------------------------------------------------------
        // 3) SamplerCache / PipelineCache 命中
        // ----------------------------------------------------------------
        SamplerDesc smp{};
        smp.minFilter = FilterMode::Linear;
        smp.magFilter = FilterMode::Linear;
        smp.mipmapMode = MipmapMode::Linear;
        smp.addressU = smp.addressV = smp.addressW = AddressMode::Repeat;

        SamplerHandle s1 = device.CreateSampler(smp);
        SamplerHandle s2 = device.CreateSampler(smp);
        DLT_CHECK(rec, s1.id != 0);
        DLT_CHECK(rec, s1.id == s2.id); // 缓存命中

        GraphicsPipelineDesc pdesc{};
        pdesc.vertexShader = sh;
        pdesc.fragmentShader = sh;
        pdesc.topology = PrimitiveTopology::TriangleList;
        PipelineHandle p1 = device.CreatePipeline(pdesc);
        PipelineHandle p2 = device.CreatePipeline(pdesc);
        DLT_CHECK(rec, p1.id != 0);
        DLT_CHECK(rec, p1.id == p2.id); // 缓存命中

        // ----------------------------------------------------------------
        // 4) 元数据注册表查询
        // ----------------------------------------------------------------
        const RHIBuffer* pBuf = device.FindBuffer(vb);
        const RHITexture* pTex = device.FindTexture(tex);
        const RHIShader* pSh = device.FindShader(sh);
        DLT_CHECK(rec, pBuf != nullptr);
        DLT_CHECK(rec, pTex != nullptr);
        DLT_CHECK(rec, pSh != nullptr);

        // ----------------------------------------------------------------
        // 5) 帧循环：BeginFrame + Submit + Present
        // ----------------------------------------------------------------
        for (int f = 0; f < 4; ++f)
        {
            device.BeginFrame();
            RenderCommandList* cmd = device.AcquireCommandList();
            DLT_CHECK(rec, cmd != nullptr);

            if (cmd)
            {
                Viewport vp{};
                vp.width = 64;
                vp.height = 64;
                cmd->SetViewport(vp);
                cmd->BindPipeline(p1);
                cmd->Draw(3, 1, 0, 0);
            }
            device.Submit(cmd);
            device.Present();
        }

        // ----------------------------------------------------------------
        // 6) Destroy + 注册表立即下线
        // ----------------------------------------------------------------
        device.Destroy(vb);
        device.Destroy(tex);
        device.Destroy(sh);
        device.Destroy(s1);
        device.Destroy(p1);

        DLT_CHECK(rec, device.FindBuffer(vb) == nullptr);
        DLT_CHECK(rec, device.FindTexture(tex) == nullptr);
        DLT_CHECK(rec, device.FindShader(sh) == nullptr);

        // ----------------------------------------------------------------
        // 7) Shutdown 兜底（应触发 FlushAllPendingDestroys）
        // ----------------------------------------------------------------
        device.Shutdown();

        LOG_INFO("DeviceLifecycleTest",
                 "end (failures=%d)",
                 rec.failures);
        return rec.failures;
    }

#undef DLT_CHECK
}
