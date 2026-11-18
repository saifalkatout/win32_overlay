#include <windows.h>

#include "WatermarkWindow.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // Must run before any window is created, including message boxes.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WatermarkWindow window;
    if (!window.Create(hInstance)) {
        return 1;
    }

    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        window.Render();
    }
}
