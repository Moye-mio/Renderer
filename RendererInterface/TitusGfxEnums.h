#pragma once
// ============================================================================
// RendererInterface - TitusGfxEnums.h
// 业务侧可见的"接口层枚举入口"。外部模块如需使用 TitusRHI::GBackend /
// TitusRHI::GThreadingMode 等门面 API 形参所用枚举，应当 #include 本头
// （或者直接 include 已经间接包含本头的 "RendererInterface/TitusGfx.h"）。
//
// 本头只是 RendererInterface 内部对 RendererCore 中 enum-only 头的"转发"：
//   - 业务模块的 include 路径里只需要存在 $(SolutionDir)，即可解析
//     "RendererInterface/..." 与下方 "RendererCore/..." 前缀；
//   - 库工程同样在 AdditionalIncludeDirectories 中包含 $(SolutionDir)，
//     跨模块统一使用 "模块/头文件.h"，不再依赖 "../" 相对路径；
//   - CI 静态扫描脚本（tools/check_no_backend_headers.bat）只扫描业务模块
//     自身源码中的 #include 字面文本，不做预处理展开，因此不会误报
//     "Renderer*/..." 后端头的暴露。
//
// 设计取向：仅暴露真正属于"门面 API 形参/返回值"的轻量枚举头，避免把
// GDescs / GHandle / IGDevice 等"实现侧"头一起拉进来——后者由
// TitusGfxPass.h 单独承担。
// ============================================================================

// GBackend：APP::SetBackend / GetBackend / GDeviceFactory::Create 形参
#include "RendererCore/GEnums.h"

// GThreadingMode：APP::SetThreadingMode / GetThreadingMode 形参
#include "RendererCore/GThreadingMode.h"

// ----------------------------------------------------------------------------
// ERenderPassEvent —— Pass 排序事件（GBuffer / AfterGBuffer / Lighting / FinalBlit ...）
//
// 业务侧（如 001_Reflective_shadow_map/main.cpp）需要在不 include
// RendererCore/* 的前提下使用 `TitusRHI::ERenderPassEvent::GBuffer` 等枚举
// 值；权威定义位于 RendererCore/IRenderPass.h，且该头是轻量头（仅含
// ERenderPassEvent 枚举 + IRenderPass 抽象类 + 两个前向声明，无任何后端
// 头依赖），因此这里直接转发，不构成"业务能看到 RendererCore 头"的语义
// 泄露——CI 静态扫描只检查业务源码自身的 #include 字面文本。
// ----------------------------------------------------------------------------
#include "RendererCore/IRenderPass.h"
