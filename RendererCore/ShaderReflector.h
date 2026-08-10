#pragma once
// ============================================================================
// RendererCore - ShaderReflector
// 把 "shader 字节码 / 文本" 反射为 ReflectionInfo（ResourceBinding 列表 +
// PushConstantRange 列表）。这一模块对外暴露 3 条路径：
//   1. ReflectFromSPIRV  : 业界标准做法，编译期通过 glslangValidator/glslc 把
//                          .glsl 编成 .spv；运行期或构建期再用 SPIRV-Cross
//                          反射出 binding 表。VK 与 GL 都能消费同一份反射。
//                          （需开启宏 TITUS_ENABLE_SPIRV_CROSS 才会真正生效；
//                            未开启时返回 false，调用方可降级到 ReflectFromHints）
//   2. ReflectFromGLSLSource : 极简的"基于关键字扫描"的回退实现，能识别
//                          uniform sampler2D / layout(binding=N) uniform Block
//                          等常见声明，覆盖项目里现有 GL shader 的大部分情况。
//   3. ReflectFromHints  : 完全由调用方提供 ResourceBinding 数组（手填）。
//                          默认走的路径——上层把约定 binding 直接
//                          填到 ShaderDesc.reflection 里。
// ============================================================================
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "GDescs.h"

namespace TitusRHI
{
    class ShaderReflector
    {
    public:
        // 1) SPIR-V 反射（推荐路径）
        //  - spv: SPIR-V 32-bit word 流（与 vkCreateShaderModule 入参一致）
        //  - wordCount: spv 数组长度（不是字节数）
        //  - stage: VS / FS / CS 等，反射后所有 binding.stages 都会被或上 stage
        //  - out: 输出反射信息；调用方多次调用时会"追加"binding（同 set/binding 自动合并 stage）
        // 返回 true 表示反射成功；返回 false 表示当前编译未启用 SPIRV-Cross
        // 或字节码非法（错误日志写到 stderr）。
        static bool ReflectFromSPIRV(const uint32_t* spv,
                                     size_t          wordCount,
                                     ShaderStage     stage,
                                     ReflectionInfo& out);

        // 2) GLSL 文本反射（回退路径，正则扫描；不能保证 100% 准确，
        //    适合"老 GL shader 临时上车"）
        static bool ReflectFromGLSLSource(const std::string& source,
                                          ShaderStage        stage,
                                          ReflectionInfo&    out);

        // 3) 业务侧手填的 ResourceBinding 数组直接 append 进 out，并合并 stage
        //    （便于上层一次性 push 所有 stage 的 binding 后做一次去重）
        static void ReflectFromHints(const std::vector<ResourceBinding>& hints,
                                     ShaderStage                         stage,
                                     ReflectionInfo&                     out);

        // 工具：合并两份 ReflectionInfo（典型场景：把 VS 和 FS 的反射合并）。
        // 同 (set, binding) 的项会把 stages 做按位或；name/type/count 取已有项。
        static void Merge(ReflectionInfo& dst, const ReflectionInfo& src);

        // 工具：判断当前 build 是否启用了 SPIRV-Cross 真反射
        static constexpr bool IsSPIRVCrossEnabled() noexcept
        {
        #if defined(TITUS_ENABLE_SPIRV_CROSS)
            return true;
        #else
            return false;
        #endif
        }
    };
} // namespace TitusRHI
