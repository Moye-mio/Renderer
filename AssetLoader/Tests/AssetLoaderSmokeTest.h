#pragma once
// ============================================================================
// AssetLoader - Tests/AssetLoaderSmokeTest.h
// 纯 CPU 烟雾测试：不依赖 GPU / 窗口；可在 CI 上运行。
// 入口：TitusAsset::Tests::RunAssetLoaderSmokeTests(rootDir)
// ============================================================================
#include <cstdio>
#include <string>

#include "Logger.h"
#include "AssetLoader.h"

namespace TitusAsset
{
    namespace Tests
    {
        struct AssetTestRecorder
        {
            int failures = 0;
            void Check(bool cond, const char* what, const char* file, int line)
            {
                if (!cond)
                {
                    ++failures;
                    LOG_ERROR("AssetLoaderSmokeTest",
                             "FAIL %s  (%s:%d)",
                             what, file, line);
                }
            }
        };

        #define ALT_CHECK(rec, cond) (rec).Check((cond), #cond, __FILE__, __LINE__)

        // rootDir：项目根目录（用于解析 Model/ 与 Fonts/ 资源路径）
        // 任意空 / 缺失资源都不会 hard-fail，只 best-effort 校验。
        inline int RunAssetLoaderSmokeTests(const std::string& rootDir)
        {
            LOG_INFO("AssetLoaderSmokeTest", "begin (root=%s)",
                     rootDir.c_str());

            AssetTestRecorder rec;

            // 1) FileSystem 工具
            ALT_CHECK(rec, GetExtensionLower("a/b/c.PNG") == "png");
            ALT_CHECK(rec, GetExtensionLower("a/b/c.HDR") == "hdr");
            ALT_CHECK(rec, GetExtensionLower("a/b/file") == "");
            ALT_CHECK(rec, !GetDirectory("a/b/c.png").empty());
            ALT_CHECK(rec, GetFileName("a/b/c.png") == "c.png");
            ALT_CHECK(rec, JoinPath("a/b", "c.png") == "a/b/c.png");
            ALT_CHECK(rec, JoinPath("a/b/", "c.png") == "a/b/c.png");

            // 2) Image 加载（best-effort：只在文件存在时校验）
            const std::string imgCandidate = rootDir + "Model/teapot/default.png";
            std::vector<uint8_t> raw;
            if (ReadAllBytes(imgCandidate, raw))
            {
                auto img = LoadImage2D(imgCandidate);
                ALT_CHECK(rec, img != nullptr);
                if (img)
                {
                    ALT_CHECK(rec, img->width  > 0);
                    ALT_CHECK(rec, img->height > 0);
                    ALT_CHECK(rec, !img->mips.empty());
                    ALT_CHECK(rec, !img->mips[0].pixels.empty());
                }
            }
            else
            {
                LOG_INFO("AssetLoaderSmokeTest",
                         "skip image test (file not found: %s)",
                         imgCandidate.c_str());
            }

            // 3) Model 加载（best-effort）
            const std::string modelCandidate = rootDir + "Model/teapot/teapot.obj";
            if (ReadAllBytes(modelCandidate, raw))
            {
                ModelAssetData model;
                ModelLoadOptions opts;
                opts.loadTextures = false; // 烟雾测试只看几何
                const bool ok = LoadModel(modelCandidate, model, opts);
                ALT_CHECK(rec, ok);
                ALT_CHECK(rec, !model.meshes.empty());
                if (!model.meshes.empty())
                {
                    ALT_CHECK(rec, !model.meshes[0].vertices.empty());
                    ALT_CHECK(rec, !model.meshes[0].indices.empty());
                }
            }
            else
            {
                LOG_INFO("AssetLoaderSmokeTest",
                         "skip model test (file not found: %s)",
                         modelCandidate.c_str());
            }

            LOG_INFO("AssetLoaderSmokeTest",
                     "end (failures=%d)",
                     rec.failures);
            return rec.failures;
        }

        #undef ALT_CHECK
    } // namespace Tests
} // namespace TitusAsset
