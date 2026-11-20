#include <windows.h>
#include "HrError.h"
#include "WatermarkWindow.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WatermarkWindow window;
    try {
        window.Create(hInstance);
    }
    catch (const HrError& e) {
        wchar_t buf[512];
        swprintf_s(buf, L"%hs failed: 0x%08X\n", e.message, e.hr);
        OutputDebugStringW(buf);
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
