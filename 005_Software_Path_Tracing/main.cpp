// ============================================================================
// 005_Software_Path_Tracing - main.cpp
//
// 光线追踪算法测试台：空的 Cornell Box + 两个互不相交的球，所有物体共用同一份
// 白色漫反射材质，唯一光源是贴着天花板的矩形面光源。
//
// 场景是解析几何（六个盒面 + 两个球 + 一个矩形光源），不建加速结构、不上传
// 任何 mesh，求交全在 RayTrace_FS.glsl 里做，所以默认 OpenGL 也能跑，
// `--backend=vk` 走同一份 shader。
//
// 因为几何和材质都被压到最简，画面差异只可能来自光线传输算法本身——这正是
// 这个工程要的：换算法、调采样数，直接看噪点与收敛速度。
// ============================================================================
#include <cstring>
#include <memory>
#include <string>

#include "Logger.h"

#include "RendererInterface/TitusGfx.h"
#include "RendererInterface/TitusGfxImGui.h"
#include "RendererInterface/TitusGfxOverlay.h"
#include "RendererInterface/TitusGfxPass.h"

#include "CornellBoxScene.h"
#include "RayTracePass.h"
#include "RayTracingContext.h"

#ifndef SOLUTION_DIR
#define SOLUTION_DIR ""
#endif

int main(int argc, char** argv)
{
    using namespace TitusRHI;
    const char* kLog = "005_Software_Path_Tracing";

    TitusBasic::Logger::Instance().Init(kLog);

    APP::ParseCommandLine(argc, argv);
    if (APP::GetBackend() == GBackend::Unknown)
        APP::SetBackend(GBackend::OpenGL);

    RTTechnique startMode = RTTechnique::PathTracing;
    // 自动截图会把 overlay 一起拍进去，出 Result 图时用 --no-gui 关掉。
    bool enableGui = true;
    for (int i = 1; i < argc; ++i)
    {
        const char* a = argv[i];
        if (std::strcmp(a, "--no-gui") == 0)
        {
            enableGui = false;
            continue;
        }
        if (std::strncmp(a, "--mode=", 7) != 0)
            continue;
        const char* v = a + 7;
        if (std::strcmp(v, "normal") == 0)
            startMode = RTTechnique::Normal;
        else if (std::strcmp(v, "direct") == 0)
            startMode = RTTechnique::DirectLight;
        else if (std::strcmp(v, "ao") == 0)
            startMode = RTTechnique::AmbientOcclusion;
        else if (std::strcmp(v, "pt") == 0 || std::strcmp(v, "pathtracing") == 0)
            startMode = RTTechnique::PathTracing;
        else
            LOG_STREAM_ERROR(kLog) << "Unknown --mode value: " << v
                << " (expected normal|direct|ao|pt)";
    }

    const char* backendName =
        (APP::GetBackend() == GBackend::OpenGL) ? "OpenGL" :
        (APP::GetBackend() == GBackend::Vulkan) ? "Vulkan" : "Null";
    LOG_STREAM_INFO(kLog) << "backend = " << backendName
        << ", validation = " << (APP::GetEnableValidation() ? "on" : "off");

    // Cornell Box 是方的，窗口也开成方的，免得画面上下留黑边。
    WINDOW_KEYWORD::SetWindowSize(1280, 1280);
    WINDOW_KEYWORD::SetIsCursorDisable(false);
    WINDOW_KEYWORD::SetWindowTitle("005_Software_Path_Tracing");
    COMPONENT_CONFIG::SetIsEnableGUI(enableGui);

    APP::InitApp();

    CornellBoxScene scene;

    {
        // 机位在盒外、正对开口。fovY 取 40°（Cornell Box 原始数据也是 39.3°），
        // 视锥到后墙时刚好比盒子宽一点，侧墙 / 地板 / 天花板都能收进画面。
        CAMERA::FlyCameraConfig cfg{};
        cfg.position  = scene.DefaultCameraPosition();
        cfg.yawDeg    = -90.0f; // 朝 -Z
        cfg.pitchDeg  = 0.0f;
        cfg.fovDeg    = 40.0f;
        cfg.aspect    = 0.0f;   // 跟随窗口
        cfg.nearPlane = 0.05f;
        cfg.farPlane  = 100.0f;
        cfg.moveSpeed = 1.5f;   // 盒子只有 4 米宽，默认速度太快不好停
        CAMERA::SetBuiltinFlyCameraConfig(cfg);
        CAMERA::EnableBuiltinFlyCamera(true);
    }

    LOG_STREAM_INFO(kLog)
        << "Cornell Box min=(" << scene.boxMin.x << "," << scene.boxMin.y << ","
        << scene.boxMin.z << ") max=(" << scene.boxMax.x << "," << scene.boxMax.y << ","
        << scene.boxMax.z << "), spheres disjoint = "
        << (scene.SpheresDisjoint() ? "yes" : "no");

    {
        RayTracingContext ctx;
        ctx.mode = startMode;

        auto rayTracePass = std::make_shared<RayTracePass>();
        rayTracePass->SetScene(&scene);
        rayTracePass->SetContext(&ctx);
        APP::AddPass(rayTracePass);

        OVERLAY::AddPanel("Ray Tracing", [&ctx, &scene]()
        {
            // 任何影响光线传输的改动都要把累积清零，否则新旧样本会被混在一起。
            bool dirty = false;

            int m = static_cast<int>(ctx.mode);
            bool modeChanged =
                ImGui::RadioButton("Normal", &m, static_cast<int>(RTTechnique::Normal));
            modeChanged = ImGui::RadioButton("Direct Light", &m,
                static_cast<int>(RTTechnique::DirectLight)) || modeChanged;
            modeChanged = ImGui::RadioButton("Ambient Occlusion", &m,
                static_cast<int>(RTTechnique::AmbientOcclusion)) || modeChanged;
            modeChanged = ImGui::RadioButton("Path Tracing", &m,
                static_cast<int>(RTTechnique::PathTracing)) || modeChanged;
            if (modeChanged)
            {
                ctx.mode = static_cast<RTTechnique>(m);
                dirty = true;
            }

            ImGui::Separator();
            if (ctx.maxAccumSamples > 0)
            {
                ImGui::Text("Accumulated: %u / %d spp",
                    ctx.accumulatedSamples, ctx.maxAccumSamples);
                if (ctx.accumulatedSamples >= static_cast<uint32_t>(ctx.maxAccumSamples))
                    ImGui::TextDisabled("converged, tracing paused");
            }
            else
            {
                ImGui::Text("Accumulated: %u spp", ctx.accumulatedSamples);
            }
            if (ImGui::Button("Restart Accumulation"))
                dirty = true;

            dirty = ImGui::SliderInt("Samples / Frame", &ctx.samplesPerFrame, 1, 32) || dirty;
            dirty = ImGui::SliderInt("Max Accum (spp)", &ctx.maxAccumSamples, 0, 8192) || dirty;

            if (ctx.mode == RTTechnique::DirectLight)
            {
                ImGui::Separator();
                // 采样数就是半影质量的旋钮：1 时半影全是噪点，加大才平滑。
                dirty = ImGui::SliderInt("Light Samples", &ctx.lightSamples, 1, 64) || dirty;
            }

            if (ctx.mode == RTTechnique::AmbientOcclusion)
            {
                ImGui::Separator();
                dirty = ImGui::SliderFloat("AO Radius", &ctx.aoRadius, 0.05f, 4.0f) || dirty;
                dirty = ImGui::SliderInt("AO Samples", &ctx.aoSamples, 1, 64) || dirty;
            }

            if (ctx.mode == RTTechnique::PathTracing)
            {
                ImGui::Separator();
                dirty = ImGui::SliderInt("Max Bounces", &ctx.maxBounces, 1, 32) || dirty;
                dirty = ImGui::Checkbox("NEE (sample light directly)", &ctx.enableNee) || dirty;
                // overlay 用的 ImGui 字体没有 CJK 字形，这里的提示必须是 ASCII。
                ImGui::TextDisabled("off = cosine hits only, much noisier");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Scene (all white)");
            dirty = ImGui::SliderFloat("Albedo", &scene.albedo, 0.0f, 1.0f) || dirty;
            dirty = ImGui::SliderFloat("Light Emission", &scene.lightEmission, 0.0f, 40.0f)
                || dirty;
            dirty = ImGui::SliderFloat2("Light Half Size", &scene.lightHalfSize.x, 0.1f, 1.8f)
                || dirty;

            dirty = ImGui::SliderFloat3("Sphere 0", &scene.sphere0Center.x, -2.0f, 4.0f) || dirty;
            dirty = ImGui::SliderFloat("Sphere 0 Radius", &scene.sphere0Radius, 0.1f, 1.5f)
                || dirty;
            dirty = ImGui::SliderFloat3("Sphere 1", &scene.sphere1Center.x, -2.0f, 4.0f) || dirty;
            dirty = ImGui::SliderFloat("Sphere 1 Radius", &scene.sphere1Radius, 0.1f, 1.5f)
                || dirty;

            if (!scene.SpheresDisjoint())
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "spheres now intersect");
            if (!scene.SpheresInsideBox())
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "sphere pokes through a wall");

            // 曝光与色调映射只作用在显示阶段，累积缓冲里存的是线性能量，
            // 所以拖这两项不用重新累积。
            ImGui::Separator();
            ImGui::TextUnformatted("Display");
            if (ctx.mode == RTTechnique::Normal)
            {
                ImGui::TextDisabled("normals are written straight out");
            }
            else
            {
                ImGui::SliderFloat("Exposure", &ctx.exposure, 0.05f, 8.0f);
                int tm = static_cast<int>(ctx.toneMap);
                const char* tmItems[] = { "None", "Reinhard", "ACES" };
                if (ImGui::Combo("Tone Map", &tm, tmItems, IM_ARRAYSIZE(tmItems)))
                    ctx.toneMap = static_cast<RTToneMap>(tm);
            }

            if (dirty)
                ctx.accumDirty = true;
        });

        while (!APP::ShouldClose())
            APP::UpdateApp();

        APP::WaitIdle();
        OVERLAY::ClearPanels();
    }

    APP::ShutdownApp();
    return 0;
}
