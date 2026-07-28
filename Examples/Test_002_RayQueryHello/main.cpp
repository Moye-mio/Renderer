// ============================================================================
// 0xx_RayQueryHello - main.cpp
// Vulkan Ray Query 端到端最小示例（需求 13）。
// 仅 include RendererInterface 门面头、仅链接 RendererInterface + Basic；
// 业务侧只使用 TitusRHI 后端无关抽象，不接触任何 VkXxx / RendererVK 头。
//
// 运行：默认 Vulkan 后端。支持光追的 GPU 上会显示以重心坐标着色的三角形；
// 不支持时优雅提示并仅清屏（红底）。
// 光追依赖 Direct 渲染模式（Threaded/Client 路径当前不支持 Compute/AS）。
// ============================================================================
#include <cstring>
#include <memory>

#include "Logger.h"

#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxPass.h"

#include "RayQueryPass.h"
#include "RayPipelinePass.h"
#include "DynamicScenePass.h"

int main(int argc, char** argv)
{
    using namespace TitusRHI;

    TitusBasic::Logger::Instance().Init("0xx_RayQueryHello");

    // 命令行选择模式（默认路线 A Ray Query）：
    //   --rtpipeline : 路线 B（RT Pipeline + SBT + TraceRays）
    //   --dynamic    : P2 动态场景（AS 管理层 + 多实例 BLAS 去重 + 每帧 TLAS refit）
    // 本示例强制 Vulkan（只有 VK 后端实现 KHR 光追）。
    bool useRTPipeline = false;
    bool useDynamic    = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!argv[i]) continue;
        if (std::strcmp(argv[i], "--rtpipeline") == 0) useRTPipeline = true;
        if (std::strcmp(argv[i], "--dynamic")    == 0) useDynamic    = true;
    }

    APP::ParseCommandLine(argc, argv);
    APP::SetBackend(GBackend::Vulkan);

    WINDOW_KEYWORD::SetWindowSize(1280, 720);
    WINDOW_KEYWORD::SetWindowTitle(useDynamic    ? "0xx_RayQueryHello [Dynamic Scene]"
                                 : useRTPipeline  ? "0xx_RayQueryHello [RT Pipeline]"
                                                  : "0xx_RayQueryHello [Ray Query]");

    APP::InitApp();

    std::shared_ptr<IRenderPass> pass;
    if      (useDynamic)    pass = std::make_shared<DynamicScenePass>();
    else if (useRTPipeline) pass = std::make_shared<RayPipelinePass>();
    else                    pass = std::make_shared<RayQueryPass>();
    APP::AddPass(pass);

    while (!APP::ShouldClose())
    {
        APP::UpdateApp();
    }

    APP::WaitIdle();
    APP::ShutdownApp();
    return 0;
}
