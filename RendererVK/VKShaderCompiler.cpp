// ============================================================================
// RendererVK - VKShaderCompiler.cpp
// 用 glslang 在 *运行时* 把 GLSL 源码编成 SPIR-V，从而 VK 后端可以与 GL 后端
// 共用同一份 *.glsl 文件，无需预编译 .spv 流程。
//
// glslang 由 Vulkan SDK（>= 1.2）自带，只需链接：
//   Debug   : glslangd.lib + MachineIndependentd.lib + GenericCodeGend.lib
//             + OSDependentd.lib + SPIRVd.lib + glslang-default-resource-limitsd.lib
//   Release : 同上去掉 d 后缀
// 链接配置已经在 RendererVK.vcxproj 的 <Link>/<AdditionalDependencies> 中声明。
// ============================================================================
#include "VKShaderCompiler.h"

#include <atomic>
#include <cstring>
#include <iostream>
#include "Logger.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

namespace TitusVkGraphics
{
    using TitusRHI::ShaderStage;

    namespace
    {
        // glslang 进程级引用计数：多个 VKDevice 实例共存时不要重复 Init/Finalize
        std::atomic<int> gGlslangRefCount{ 0 };

        EShLanguage ToEShLanguage(ShaderStage stage)
        {
            switch (stage)
            {
            case ShaderStage::Vertex:         return EShLangVertex;
            case ShaderStage::Fragment:       return EShLangFragment;
            case ShaderStage::Geometry:       return EShLangGeometry;
            case ShaderStage::Compute:        return EShLangCompute;
            case ShaderStage::TessControl:    return EShLangTessControl;
            case ShaderStage::TessEvaluation: return EShLangTessEvaluation;
            // 光追管线阶段：需 SPIR-V ≥ 1.4 目标（已设为 1.5）
            // 与 GL_EXT_ray_tracing 扩展。
            case ShaderStage::RayGen:         return EShLangRayGen;
            case ShaderStage::Miss:           return EShLangMiss;
            case ShaderStage::ClosestHit:     return EShLangClosestHit;
            case ShaderStage::AnyHit:         return EShLangAnyHit;
            case ShaderStage::Intersection:   return EShLangIntersect;
            case ShaderStage::Callable:       return EShLangCallable;
            default:                          return EShLangVertex;
            }
        }
    }

    bool GuessIsSpirv(const void* bytes, size_t size)
    {
        if (!bytes || size < 4) return false;
        const uint32_t* p = static_cast<const uint32_t*>(bytes);
        // SPIR-V magic number（小端）
        return p[0] == 0x07230203u;
    }

    void InitGlslang()
    {
        if (gGlslangRefCount.fetch_add(1) == 0)
        {
            glslang::InitializeProcess();
        }
    }

    void FinalizeGlslang()
    {
        if (gGlslangRefCount.fetch_sub(1) == 1)
        {
            glslang::FinalizeProcess();
        }
    }

    bool CompileGlslToSpirv(ShaderStage             stage,
                            const char*             source,
                            size_t                  sourceLen,
                            const char*             debugName,
                            std::vector<uint32_t>&  outSpv)
    {
        if (!source) return false;
        if (sourceLen == 0) sourceLen = std::strlen(source);

        InitGlslang();

        const EShLanguage  lang   = ToEShLanguage(stage);
        const char*        srcPtr = source;
        const int          srcLen = static_cast<int>(sourceLen);
        const char*        srcName = debugName ? debugName : "<inline>";

        glslang::TShader shader(lang);
        shader.setStringsWithLengthsAndNames(&srcPtr, &srcLen, &srcName, 1);
        shader.setEnvInput (glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan,    glslang::EShTargetVulkan_1_2);
        // 光追：目标 SPIR-V 1.5（≥ 1.4），使含
        // `#extension GL_EXT_ray_query` 的 compute/fragment 着色器可编译，
        // 产出字节码可被 vkCreateShaderModule 接受。
        shader.setEnvTarget(glslang::EShTargetSpv,       glslang::EShTargetSpv_1_5);

        // 修复：glslang 的 setEnvClient(EShClientVulkan,...) 并不会自动
        // `#define VULKAN`。我们的 GLSL 用 `#ifdef VULKAN` 区分两端 binding 装饰
        // (LAYOUT_BIND 宏) 与 push_constant block，必须在此显式注入预定义宏，
        // 否则 GLSL 走 GL 分支：所有 binding 不带 set/explicit binding，glslang
        // 只能按"自动分配"规则给 UBO/Image/Sampler 重新编号 → 与 cpp 端
        // ResourceBinding 声明严重错配（典型表现：Validation Layer 报
        // "Set 0 Binding 0, variable u_Matrices4ProjectionWorld is being used in
        //  draw but has never been updated"）。同时 push_constant 块也不会生成，
        // GLSL 顶层 `uniform mat4 u_ModelMatrix;` 在 Vulkan 规则下会被 glslang
        // 包装成默认 UBO，进一步污染 binding 布局。
        shader.setPreamble("#define VULKAN 100\n");

        // 与 GLSL 4.30 行为兼容；同时打开 Vulkan 规则
        const int defaultVersion = 430;
        const EShMessages messages = static_cast<EShMessages>(
            EShMsgSpvRules | EShMsgVulkanRules);

        const TBuiltInResource* resources = GetDefaultResources();

        if (!shader.parse(resources, defaultVersion, /*forwardCompatible*/ false, messages))
        {
            LOG_STREAM_ERROR("VKShaderCompiler") << "parse failed: " << srcName << "\n"
                      << "  info  : " << shader.getInfoLog() << "\n"
                      << "  debug : " << shader.getInfoDebugLog();
            FinalizeGlslang();
            return false;
        }

        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages))
        {
            LOG_STREAM_ERROR("VKShaderCompiler") << "link failed: " << srcName << "\n"
                      << "  info  : " << program.getInfoLog() << "\n"
                      << "  debug : " << program.getInfoDebugLog();
            FinalizeGlslang();
            return false;
        }

        // 输出 SPIR-V word 数组
        glslang::SpvOptions spvOpts{};
        spvOpts.generateDebugInfo = false;
        spvOpts.disableOptimizer  = true;   // 调试期保留可读结构；后续可改 false
        spvOpts.optimizeSize      = false;
        spvOpts.disassemble       = false;
        spvOpts.validate          = false;

        glslang::TIntermediate* im = program.getIntermediate(lang);
        if (!im)
        {
            LOG_STREAM_ERROR("VKShaderCompiler") << "no intermediate produced for " << srcName;
            FinalizeGlslang();
            return false;
        }

        outSpv.clear();
        glslang::GlslangToSpv(*im, outSpv, &spvOpts);

        FinalizeGlslang();
        return !outSpv.empty();
    }
}
