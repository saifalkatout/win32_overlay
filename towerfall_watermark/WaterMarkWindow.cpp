#include "WatermarkWindow.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "HrError.h"

namespace {

    constexpr wchar_t  kClassName[] = L"ScreenWatermarkWindow";
    constexpr UINT_PTR kTimerId = 1;
    constexpr int      kQuitHotkeyId = 1;

    // ---- Helpers ---------------------------------------------------------------

    RECT GetPrimaryMonitorRect()
    {
        HMONITOR mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        return mi.rcMonitor;
    }

    bool RegisterWindowClass(HINSTANCE instance, WNDPROC proc)
    {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = proc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        if (RegisterClassExW(&wc)) {
            return true;
        }
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool IsNotepadInForeground()
    {
        wchar_t buf[256];
        const int len = GetClassNameW(GetForegroundWindow(), buf, ARRAYSIZE(buf));
        return std::wstring_view(buf, len) == L"Notepad";
    }

}

// ---- Creation and destruction ------------------------------------------------

WatermarkWindow::~WatermarkWindow()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

void WatermarkWindow::Create(HINSTANCE instance)
{
    if (!RegisterWindowClass(instance, &WatermarkWindow::WndProc)) {
        throw HrError{ HRESULT_FROM_WIN32(GetLastError()), "RegisterWindowClass" };
    }

    const RECT r = GetPrimaryMonitorRect();
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED
        | WS_EX_TRANSPARENT
        | WS_EX_TOPMOST
        | WS_EX_TOOLWINDOW
        | WS_EX_NOACTIVATE
        | WS_EX_NOREDIRECTIONBITMAP,
        kClassName, L"Watermark",
        WS_POPUP,
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, instance, this);
    if (!hwnd) {
        throw HrError{ HRESULT_FROM_WIN32(GetLastError()), "CreateWindowExW" };
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    m_width = static_cast<UINT>(client.right - client.left);
    m_height = static_cast<UINT>(client.bottom - client.top);
    m_dpi = static_cast<float>(GetDpiForWindow(hwnd));

    if (!m_renderer.Initialize(hwnd, m_width, m_height, m_dpi)) {
        throw HrError{ E_FAIL, "Renderer::Initialize" };
    }

    RegisterHotKey(hwnd, kQuitHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q');
    SetTimer(hwnd, kTimerId, 1000, nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
}

// ---- Drawing and animation ---------------------------------------------------

void WatermarkWindow::UpdateAnimation(float seconds)
{
    const float screenW = static_cast<float>(m_width) * 96.0f / m_dpi;
    const float screenH = static_cast<float>(m_height) * 96.0f / m_dpi;

    m_pos.x += m_vel.x * seconds;
    m_pos.y += m_vel.y * seconds;

    if (m_pos.x < 0.0f)             { m_pos.x = 0.0f;             m_vel.x = std::fabs(m_vel.x); }
    if (m_pos.x + m_side > screenW) { m_pos.x = screenW - m_side; m_vel.x = -std::fabs(m_vel.x); }
    if (m_pos.y < 0.0f)             { m_pos.y = 0.0f;             m_vel.y = std::fabs(m_vel.y); }
    if (m_pos.y + m_side > screenH) { m_pos.y = screenH - m_side; m_vel.y = -std::fabs(m_vel.y); }
}

void WatermarkWindow::Render()
{
    if (!m_hwnd) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    float seconds = 0.0f;
    if (m_lastFrame != std::chrono::steady_clock::time_point{}) {
        seconds = std::chrono::duration<float>(now - m_lastFrame).count();
    }
    m_lastFrame = now;
    UpdateAnimation(std::min(seconds, 0.1f));

    const D2D1_RECT_F square = D2D1::RectF(m_pos.x, m_pos.y, m_pos.x + m_side, m_pos.y + m_side);
    if (!m_renderer.Render(IsNotepadInForeground() ? &square : nullptr)) {
        Sleep(500);
    }
}

// ---- Messages ----------------------------------------------------------------

LRESULT CALLBACK WatermarkWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WatermarkWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<WatermarkWindow*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else {
        self = reinterpret_cast<WatermarkWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT WatermarkWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        ValidateRect(m_hwnd, nullptr);
        return 0;
    }

    case WM_SIZE: {
        m_width = LOWORD(lp);
        m_height = HIWORD(lp);
        m_renderer.Resize(m_width, m_height);
        return 0;
    }

    case WM_TIMER: {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    }

    case WM_DPICHANGED: {
        m_dpi = static_cast<float>(LOWORD(wp));
        m_renderer.SetDPI(m_dpi);
        FitToPrimaryMonitor();
        return 0;
    }

    case WM_DISPLAYCHANGE: {
        FitToPrimaryMonitor();
        return 0;
    }

    case WM_HOTKEY: {
        if (wp == kQuitHotkeyId) {
            DestroyWindow(m_hwnd);
        }
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(m_hwnd, kTimerId);
        UnregisterHotKey(m_hwnd, kQuitHotkeyId);
        PostQuitMessage(0);
        return 0;
    }

    case WM_NCDESTROY: {
        HWND hwnd = m_hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        m_hwnd = nullptr;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    }

    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void WatermarkWindow::FitToPrimaryMonitor()
{
    const RECT r = GetPrimaryMonitorRect();
    SetWindowPos(m_hwnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
        SWP_NOACTIVATE);
}
