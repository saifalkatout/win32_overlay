#include <windows.h>
#include <string>
#include <format>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")

namespace {
    constexpr wchar_t kClassName[] = L"WaterMarkWindow";
    constexpr COLORREF kTransparentKey = RGB(255, 0, 255);
    constexpr BYTE kOpacity = 70;
    constexpr UINT_PTR kTimerId = 1;
    constexpr int kQuitHotkeyId = 1;

    std::wstring curr_text;


    std::wstring BuildWatermarkText() {
        wchar_t user[256];
        DWORD userLen = ARRAYSIZE(user);
        GetUserNameW(user, &userLen);

        wchar_t machine[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD machineLen = ARRAYSIZE(machine);
        GetComputerNameW(machine, &machineLen);

        SYSTEMTIME t{};
        GetLocalTime(&t);

        return std::format(L"{} @ {}   {:04}-{:02}-{:02} {:02}:{:02}",
            user, machine, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    }

    RECT PrimaryMonitorRect()
    {
        HMONITOR mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{ sizeof(mi) };

        GetMonitorInfoW(mon, &mi);
        return mi.rcMonitor;
    }

    void Paint(HWND hwnd)
    {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);

        // 1. Fill the whole window with the transparent color.
        HBRUSH bg = CreateSolidBrush(kTransparentKey);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        // 2. Create a rotated font, sized for this monitor's DPI.
        const UINT dpi = GetDpiForWindow(hwnd);
        const int fontHeight = -MulDiv(18, static_cast<int>(dpi), 72); // 18 pt
        HFONT font = CreateFontW(
            fontHeight, 0,
            300, 300,                 // 30 degrees (units are tenths of a degree)
            FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            NONANTIALIASED_QUALITY,   // smooth edges would blend with magenta into pink fringes
            DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc, font);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(120, 120, 120));

        // 3. Tile the text across the window; every other row is shifted half a step.
        const int stepX = MulDiv(480, static_cast<int>(dpi), 96);
        const int stepY = MulDiv(240, static_cast<int>(dpi), 96);
        const int len = static_cast<int>(curr_text.size());

        int row = 0;
        for (int y = 0; y < rc.bottom + stepY; y += stepY, ++row) {
            const int startX = (row % 2) ? -stepX / 2 : 0;
            for (int x = startX; x < rc.right; x += stepX) {
                TextOutW(dc, x, y + stepY / 2, curr_text.c_str(), len);
            }
        }

        SelectObject(dc, oldFont);
        DeleteObject(font);
        EndPaint(hwnd, &ps);
    }

    void FitToPrimaryMonitor(HWND hwnd)
    {
        const RECT r = PrimaryMonitorRect();
        SetWindowPos(hwnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
            SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg) {
        case WM_PAINT:
            Paint(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1; // we paint everything ourselves; skipping the erase avoids flicker

        case WM_TIMER: {
            // Move back on top in case another topmost window covered us.
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            // Repaint only when the text changes (once a minute).
            std::wstring text = BuildWatermarkText();
            if (text != curr_text) {
                curr_text = std::move(text);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_DISPLAYCHANGE: // resolution changed, or a monitor was added or removed
        case WM_DPICHANGED:    // scaling setting changed
            FitToPrimaryMonitor(hwnd);
            return 0;

        case WM_HOTKEY:
            if (wp == kQuitHotkeyId) {
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, kTimerId);
            UnregisterHotKey(hwnd, kQuitHotkeyId);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

} // namespace
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // Must run before any window is created, including message boxes.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 1. Register the window class: the "type" of our window and its message handler.
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    curr_text = BuildWatermarkText();
    const RECT r = PrimaryMonitorRect();
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED         
        | WS_EX_TRANSPARENT  
        | WS_EX_TOPMOST       
        | WS_EX_TOOLWINDOW    
        | WS_EX_NOACTIVATE,   
        kClassName, L"Watermark",
        WS_POPUP,           
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        return 1;
    }
    SetLayeredWindowAttributes(hwnd, kTransparentKey, kOpacity, LWA_COLORKEY | LWA_ALPHA);

    RegisterHotKey(hwnd, kQuitHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q');
    SetTimer(hwnd, kTimerId, 1000, nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg); 
    }
    return static_cast<int>(msg.wParam);
}