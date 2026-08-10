#pragma once
// ============================================================================
// RendererVK - VKShaderCompiler
// 把 GLSL 源码通过 glslang（Vulkan SDK 自带）在线编译为
// SPIR-V 字节码。这样 GL/VK 两端共用同一份 *.glsl，无需预编译 .spv 文件。
//
// 调用契约：
//   - GuessIsSpirv(bytes, size)：嗅探 magic word 0x07230203 判断是否已是 SPIR-V
//   - CompileGlslToSpirv(stage, source, sourceLen, debugName, outSpv)：
//       源码以 `\0` 结尾或显式给 sourceLen；返回 false 时控制台打印 InfoLog。
//
// 实现细节集中在 .cpp：本头文件不暴露 glslang 类型，避免污染上游。
// ============================================================================
#include <cstdint>
#include <vector>
#include <string>

#include "RendererCore/GEnums.h"

namespace TitusVkGraphics
{
    // 是否是有效的 SPIR-V 字节流（首 4 字节为 0x07230203）
    bool GuessIsSpirv(const void* bytes, size_t size);

    // 把 GLSL 源码编译为 SPIR-V word stream（uint32_t 数组）。
    //   stage      ：着色器阶段（决定 EShLanguage）
    //   source     ：GLSL 源码字符串起点
    //   sourceLen  ：源码长度（字节）；传 0 时按 strlen 处理
    //   debugName  ：用于错误日志
    //   outSpv     ：输出 SPIR-V word 数组（成功时填充；失败保留旧内容）
    // 返回 true 表示编译成功且 outSpv 中包含可被 vkCreateShaderModule 使用的字节流。
    bool CompileGlslToSpirv(TitusRHI::ShaderStage   stage,
                            const char*               source,
                            size_t                    sourceLen,
                            const char*               debugName,
                            std::vector<uint32_t>&    outSpv);

    // glslang 进程级初始化 / 反初始化（线程安全的引用计数实现）。
    // VKDevice 在 Init/Shutdown 各自调用一次即可；多次嵌套也安全。
    void InitGlslang();
    void FinalizeGlslang();
}
