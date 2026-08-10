#include "TracySupport.h"

#include <atomic>

namespace
{
#ifdef TRACY_ENABLE
    // 默认开启：已连接 Profiler 时按需采集；ImGui 可关掉。
    std::atomic<bool> g_tracyCaptureEnabled{ true };
#endif
}

bool TitusTracyCaptureEnabled()
{
#ifdef TRACY_ENABLE
    return g_tracyCaptureEnabled.load(std::memory_order_relaxed);
#else
    return false;
#endif
}

void TitusTracySetCaptureEnabled(bool enabled)
{
#ifdef TRACY_ENABLE
    g_tracyCaptureEnabled.store(enabled, std::memory_order_relaxed);
#else
    (void)enabled;
#endif
}
