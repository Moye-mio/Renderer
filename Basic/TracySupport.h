#pragma once
// ============================================================================
// Tracy 插桩入口：请 include 本头（不要直接依赖 Third-Party 路径）。
//
// 注意：MSVC Edit and Continue（/ZI）下 __LINE__ 不是常量，ZoneScoped 会 C2131。
// Directory.Build.props 在 TitusTracyEnable=true 时已改为 /Zi。
// 若仍遇到 C2131，可在 include 本头之前手动：#define TracyLine 0
// ============================================================================

#include <tracy/Tracy.hpp>
