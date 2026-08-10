// ============================================================================
// 010_UnifiedTriangle - main.cpp
// 同一份示例工程通过 --backend=gl 或 --backend=vk 选择后端；其余代码不变。
// **仅** include RendererInterface 头、**仅**链接 RendererInterface.lib。
// CI 静态扫描脚本 tools/check_no_backend_headers.bat 会校验本目录不含
// `<vulkan/...>`、`<GL/...>`、`<glad/...>`、`<glfw3.h>`、
// `Renderer/*`、`RendererCore/*`、`RendererVK/*`、`Platform/*` 等任何后端字面 include。
// ============================================================================
#include <iostream>
#include <memory>
#include <cstring>

#include "Logger.h"

#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxPass.h"

#include "TrianglePass.h"

int main(int argc, char** argv)
{
    using namespace TitusRHI;

    // 0.0) Logger 尽早初始化
    TitusBasic::Logger::Instance().Init("010_UnifiedTriangle");

    // 0) 单元测试旁路：若命中 --run-tests，则跑完 Null 后端
    //    单元测试后立即退出；本路径不创建窗口、不创建真实 GPU 设备。
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] && std::strcmp(argv[i], "--run-tests") == 0)
        {
            const int failures = APP::RunUnitTests();
            LOG_STREAM_INFO("010_UnifiedTriangle") << "unit tests done, failures="
                      << failures;
            return failures == 0 ? 0 : 1;
        }
    }

    // 1) 解析命令行（--backend=gl|vk|null、--threading=direct|threaded|nonthreaded）
    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown)
    {
        // 默认采用 Vulkan（与 010_VkTriangle 保持一致；可通过 --backend=gl 切换）
        APP::SetBackend(GBackend::Vulkan);
    }

    LOG_STREAM_INFO("010_UnifiedTriangle") << "backend = "
              << (APP::GetBackend() == GBackend::OpenGL ? "OpenGL"
                : APP::GetBackend() == GBackend::Vulkan ? "Vulkan" : "Null")
              << "  threading = "
              << static_cast<int>(APP::GetThreadingMode());

    // 2) 配置窗口标题
    WINDOW_KEYWORD::SetWindowSize(1280, 720);
    WINDOW_KEYWORD::SetWindowTitle("010_UnifiedTriangle");

    // 3) 初始化（创建窗口 + 设备 + PassScheduler）
    APP::InitApp();

    // 4) 注册示例 Pass
    auto trianglePass = std::make_shared<TrianglePass>();
    APP::AddPass(trianglePass);

    // 5) 主循环
    while (!APP::ShouldClose())
    {
        APP::UpdateApp();
    }

    // 6) 退出前等待 GPU 空闲，避免 in-flight 资源被提前销毁
    APP::WaitIdle();
    APP::ShutdownApp();
    return 0;
}
