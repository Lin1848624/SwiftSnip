#include "dpi.h"

#include <windows.h>

void InitDpiAwareness() {
    // 优先 PerMonitorV2，其次 PerMonitor，最后回退到系统 DPI 感知；三级回退保证老系统可用
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        return;
    }
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
        return;
    }
    SetProcessDPIAware();
}

