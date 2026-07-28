#pragma once
// ============================================================================
// RendererCore - ShaderReflection
// 着色器反射信息：在 GDescs.h 中已经声明 ResourceBinding / PushConstantRange /
// ReflectionInfo，本文件只是为了让"反射信息"作为一个独立可被业务/工具直接 include
// 的入口存在（任务 7 的需求 10.3）。
//
// 反射数据通常由构建期工具产出：例如 spirv-cross --reflect 输出 JSON，再由资产
// 加载器解析为 ReflectionInfo 实例。
// ============================================================================
#include "GDescs.h"

namespace TitusRHI
{
    // 当前阶段所有反射结构都直接复用 GDescs.h 中的定义：
    //   - ResourceBinding
    //   - PushConstantRange
    //   - ReflectionInfo
    // 后续若需要扩展（如顶点输入反射、SpecConstants），可在此追加。
}
