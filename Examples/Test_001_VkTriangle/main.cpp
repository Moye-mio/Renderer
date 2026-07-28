// ============================================================================
// Test_001_VkTriangle - main.cpp
// 方案 A 迁移：仅通过 RendererInterface 暴露的 `TitusRHI::*` 门面 API 启动
// 应用、注册 Pass，不再直接使用 RendererVK 的 VkInterface / RESOURCE_MANAGER。
// 默认 Vulkan 后端（呼应工程名），可用 `--backend=gl` 切换到 OpenGL。
// ============================================================================
#include <memory>
#include <cstring>

#include "Logger.h"

#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxPass.h"

#include "TrianglePass.h"

int main(int argc, char** argv)
{
    using namespace TitusRHI;

    // 0) Logger 尽早初始化
    TitusBasic::Logger::Instance().Init("Test_001_VkTriangle");

    // 1) 解析命令行（--backend=gl|vk|null、--threading=...）
    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown)
    {
        // 默认 Vulkan（与工程名保持一致；可通过 --backend=gl 切换）
        APP::SetBackend(GBackend::Vulkan);
    }

    LOG_STREAM_INFO("Test_001_VkTriangle") << "backend = "
              << (APP::GetBackend() == GBackend::OpenGL ? "OpenGL"
                : APP::GetBackend() == GBackend::Vulkan ? "Vulkan" : "Null");

    // 2) 配置窗口
    WINDOW_KEYWORD::SetWindowSize(1280, 720);
    WINDOW_KEYWORD::SetWindowTitle("Test_001_VkTriangle");
    WINDOW_KEYWORD::SetIsCursorDisable(false);

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
