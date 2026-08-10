#pragma once
// ============================================================================
// Tracy 插桩入口：请 include 本头（不要直接依赖 Third-Party 路径）。
//
// 注意：MSVC Edit and Continue（/ZI）下 __LINE__ 不是常量，ZoneScoped 会 C2131。
// Directory.Build.props 在 TitusTracyEnable=true 时已改为 /Zi，并定义
// TRACY_ENABLE + TRACY_ON_DEMAND（未连接 Profiler 时不采集）。
// 若仍遇到 C2131，可在 include 本头之前手动：#define TracyLine 0
//
// 运行时：TitusTracySetCaptureEnabled / ImGui「Tracy Capture」可在已连接时
// 暂停采集（ZoneScoped*/FrameMark/Plot 已包装；ZoneTransient* 需自行把
// active 写成 TitusTracyCaptureEnabled()）。
// ============================================================================

#include <tracy/Tracy.hpp>

// 运行时采集开关（与是否连接 Profiler 独立；需 TRACY_ENABLE 才有效）。
bool TitusTracyCaptureEnabled();
void TitusTracySetCaptureEnabled(bool enabled);

#ifdef TRACY_ENABLE

#undef ZoneScoped
#undef ZoneScopedN
#undef ZoneScopedC
#undef ZoneScopedNC
#undef ZoneScopedS
#undef ZoneScopedNS
#undef ZoneScopedCS
#undef ZoneScopedNCS
#undef FrameMark
#undef FrameMarkNamed
#undef FrameMarkStart
#undef FrameMarkEnd
#undef TracyPlot
#undef TracyPlotConfig

#define ZoneScoped \
    SuppressVarShadowWarning( ZoneNamed( ___tracy_scoped_zone, TitusTracyCaptureEnabled() ) )
#define ZoneScopedN( name ) \
    SuppressVarShadowWarning( ZoneNamedN( ___tracy_scoped_zone, name, TitusTracyCaptureEnabled() ) )
#define ZoneScopedC( color ) \
    SuppressVarShadowWarning( ZoneNamedC( ___tracy_scoped_zone, color, TitusTracyCaptureEnabled() ) )
#define ZoneScopedNC( name, color ) \
    SuppressVarShadowWarning( ZoneNamedNC( ___tracy_scoped_zone, name, color, TitusTracyCaptureEnabled() ) )

#define ZoneScopedS( depth ) \
    ZoneNamedS( ___tracy_scoped_zone, depth, TitusTracyCaptureEnabled() )
#define ZoneScopedNS( name, depth ) \
    ZoneNamedNS( ___tracy_scoped_zone, name, depth, TitusTracyCaptureEnabled() )
#define ZoneScopedCS( color, depth ) \
    ZoneNamedCS( ___tracy_scoped_zone, color, depth, TitusTracyCaptureEnabled() )
#define ZoneScopedNCS( name, color, depth ) \
    ZoneNamedNCS( ___tracy_scoped_zone, name, color, depth, TitusTracyCaptureEnabled() )

#define FrameMark \
    do { if (TitusTracyCaptureEnabled()) tracy::Profiler::SendFrameMark(nullptr); } while (0)
#define FrameMarkNamed( name ) \
    do { if (TitusTracyCaptureEnabled()) tracy::Profiler::SendFrameMark(name); } while (0)
#define FrameMarkStart( name ) \
    do { if (TitusTracyCaptureEnabled()) \
        tracy::Profiler::SendFrameMark(name, tracy::QueueType::FrameMarkMsgStart); } while (0)
#define FrameMarkEnd( name ) \
    do { if (TitusTracyCaptureEnabled()) \
        tracy::Profiler::SendFrameMark(name, tracy::QueueType::FrameMarkMsgEnd); } while (0)

#define TracyPlot( name, val ) \
    do { if (TitusTracyCaptureEnabled()) tracy::Profiler::PlotData(name, val); } while (0)
#define TracyPlotConfig( name, type, step, fill, color ) \
    do { if (TitusTracyCaptureEnabled()) \
        tracy::Profiler::ConfigurePlot(name, type, step, fill, color); } while (0)

#endif // TRACY_ENABLE
