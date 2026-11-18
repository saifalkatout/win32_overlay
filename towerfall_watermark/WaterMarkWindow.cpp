#include "WatermarkWindow.h"

#include <algorithm>
#include <cmath>
#include <format>

#pragma comment(lib, "advapi32.lib") // GetUserNameW
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

    constexpr wchar_t  kClassName[] = L"ScreenWatermarkWindow";
    constexpr UINT_PTR kTimerId = 1;
    constexpr int      kQuitHotkeyId = 1;     // Ctrl+Alt+Q
	int                kIsNotepadOpen = 0;

    // ---- Helpers ---------------------------------------------------------------

    RECT GetPrimaryMonitorRect()
    {
        // The primary monitor's top-left corner is always (0, 0).
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

} // namespace

// ---- Creation and destruction ------------------------------------------------

WatermarkWindow::~WatermarkWindow()
{
    ReleaseDeviceResources();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

bool WatermarkWindow::Create(HINSTANCE instance)
{
    if (!RegisterWindowClass(instance, &WatermarkWindow::WndProc)) {
        return false;
    }

    // Factories don't depend on the GPU, so they are created once.
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        m_d2dFactory.GetAddressOf()))) {
        return false;
    }

    const RECT r = GetPrimaryMonitorRect();
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED                // needed for clicks to pass through
        | WS_EX_TRANSPARENT          // clicks pass through
        | WS_EX_TOPMOST              // above normal windows
        | WS_EX_TOOLWINDOW           // no taskbar button, not in Alt+Tab
        | WS_EX_NOACTIVATE           // never takes focus
        | WS_EX_NOREDIRECTIONBITMAP, // no GDI surface: DirectComposition provides the content
        kClassName, L"Watermark",
        WS_POPUP,
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, instance, this);
    if (!hwnd) {
        return false;
    }

    // Layered windows need this call before they show. 255 = don't fade the whole window;
    // per-pixel transparency comes from what we draw.
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    RECT client{};
    GetClientRect(hwnd, &client);
    m_width = static_cast<UINT>(client.right - client.left);
    m_height = static_cast<UINT>(client.bottom - client.top);
    m_dpi = static_cast<float>(GetDpiForWindow(hwnd));

    if (!CreateDeviceResources()) {
        return false;
    }

    RegisterHotKey(hwnd, kQuitHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q');
    SetTimer(hwnd, kTimerId, 1000, nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return true;
}

// ---- GPU resources -----------------------------------------------------------

bool WatermarkWindow::CreateDeviceResources()
{
    if (!m_hwnd || m_width == 0 || m_height == 0) {
        return false;
    }

    // 1. Direct3D 11 device. BGRA support is required for Direct2D.
    //    If there's no usable GPU (some VMs), fall back to WARP, a software renderer.
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION, &m_d3dDevice, nullptr, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION, &m_d3dDevice, nullptr, nullptr);
    }
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_d3dDevice.As(&dxgiDevice))) {
        return false;
    }

    // 2. Swap chain: two screen-sized buffers we draw into, with per-pixel alpha.
    //    "ForComposition" means it isn't tied to a window; DirectComposition shows it.
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)))) {
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(dxgiFactory->CreateSwapChainForComposition(m_d3dDevice.Get(), &desc,
        nullptr, &m_swapChain))) {
        return false;
    }

    // 3. Direct2D device context: the object we issue drawing commands to.
    ComPtr<ID2D1Device> d2dDevice;
    if (FAILED(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice)) ||
        FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_dc))) {
        return false;
    }
    if (!CreateTargetBitmap()) {
        return false;
    }

    // Colors are (red, green, blue, alpha). A dark fill with white text
    // stays readable on both light and dark backgrounds.
    m_dc->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f), &m_fillBrush);
    m_dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &m_borderBrush);

    // 4. DirectComposition: window -> target -> visual -> swap chain.
    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_dcompDevice))) ||
        FAILED(m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget)) ||
        FAILED(m_dcompDevice->CreateVisual(&m_dcompVisual))) {
        return false;
    }
    m_dcompVisual->SetContent(m_swapChain.Get());
    m_dcompTarget->SetRoot(m_dcompVisual.Get());
    m_dcompDevice->Commit();
    return true;
}

bool WatermarkWindow::CreateTargetBitmap()
{
    // Wrap the swap chain's back buffer as a Direct2D bitmap and draw into it.
    ComPtr<IDXGISurface> surface;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)))) {
        return false;
    }
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        m_dpi, m_dpi);
    if (FAILED(m_dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &m_targetBitmap))) {
        return false;
    }
    m_dc->SetTarget(m_targetBitmap.Get());
    m_dc->SetDpi(m_dpi, m_dpi); // from here on, coordinates are in DIPs
    return true;
}

void WatermarkWindow::ReleaseDeviceResources()
{
    if (m_dc) {
        m_dc->SetTarget(nullptr);
    }
    m_dcompVisual.Reset();
    m_dcompTarget.Reset();
    m_dcompDevice.Reset();
    m_fillBrush.Reset();
    m_borderBrush.Reset();
    m_targetBitmap.Reset();
    m_dc.Reset();
    m_swapChain.Reset();
    m_d3dDevice.Reset();
}

void WatermarkWindow::Resize()
{
    if (!m_swapChain || m_width == 0 || m_height == 0) {
        return;
    }
    // The swap chain's buffers can't be resized while Direct2D still holds them.
    m_dc->SetTarget(nullptr);
    m_targetBitmap.Reset();
    if (FAILED(m_swapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0)) ||
        !CreateTargetBitmap()) {
        ReleaseDeviceResources(); // Render() will rebuild everything
    }
}

// ---- Drawing and animation ---------------------------------------------------

void WatermarkWindow::UpdateAnimation(float seconds)
{
    // Screen size in DIPs.
    const float screenW = static_cast<float>(m_width) * 96.0f / m_dpi;
    const float screenH = static_cast<float>(m_height) * 96.0f / m_dpi;

    m_pos.x += m_vel.x * seconds;
    m_pos.y += m_vel.y * seconds;

    // Bounce off the edges.
    if (m_pos.x < 0.0f) { m_pos.x = 0.0f;              m_vel.x = std::fabs(m_vel.x); }
    if (m_pos.x + m_side > screenW) { m_pos.x = screenW - m_side;  m_vel.x = -std::fabs(m_vel.x); }
    if (m_pos.y < 0.0f) { m_pos.y = 0.0f;              m_vel.y = std::fabs(m_vel.y); }
    if (m_pos.y + m_side > screenH) { m_pos.y = screenH - m_side;  m_vel.y = -std::fabs(m_vel.y); }
}

void WatermarkWindow::Render()
{
    if (!m_hwnd) {
        return;
    }
    if (!m_dc && !CreateDeviceResources()) {
        Sleep(500); // GPU not ready (e.g. driver update); try again shortly
        return;
    }

    // Time since the last frame, so the speed is the same at 60 Hz and 144 Hz.
    const auto now = std::chrono::steady_clock::now();
    float seconds = 0.0f;
    if (m_lastFrame != std::chrono::steady_clock::time_point{}) {
        seconds = std::chrono::duration<float>(now - m_lastFrame).count();
    }
    m_lastFrame = now;
    UpdateAnimation(std::min(seconds, 0.1f)); // don't jump after a pause (sleep, lock screen)

    wchar_t buf[256];
    const int len = GetClassNameW(GetForegroundWindow(), buf, ARRAYSIZE(buf));   // returns the character count
    std::wstring name(buf, len);   // copies exactly len characters


    if (name != L"Notepad") {
        kIsNotepadOpen = 0;
    }
    else {
        kIsNotepadOpen = 1;
    }
    m_dc->BeginDraw();

    m_dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)); // fully transparent

	if (kIsNotepadOpen == 1) {

        const D2D1_ROUNDED_RECT square{
            D2D1::RectF(m_pos.x, m_pos.y, m_pos.x + m_side, m_pos.y + m_side), kCorner, kCorner };
        m_dc->FillRoundedRectangle(square, m_fillBrush.Get());
        m_dc->DrawRoundedRectangle(square, m_borderBrush.Get(), 2.0f);
	}

 

    HRESULT hr = m_dc->EndDraw();
    if (SUCCEEDED(hr)) {
        hr = m_swapChain->Present(1, 0); // 1 = wait for the next screen refresh
    }

    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED ||
        hr == DXGI_ERROR_DEVICE_RESET) {
        ReleaseDeviceResources(); // GPU was reset; rebuilt on the next frame
    }
    else if (hr == DXGI_STATUS_OCCLUDED) {
        Sleep(50);                // nothing visible to draw to; don't spin
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
    case WM_PAINT:
        // Nothing to paint with GDI; just tell Windows the window is up to date.
        ValidateRect(m_hwnd, nullptr);
        return 0;

    case WM_SIZE:
        m_width = LOWORD(lp);
        m_height = HIWORD(lp);
        Resize();
        return 0;

    case WM_TIMER:
        // Stay on top, and refresh the text when the minute changes.
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;

    case WM_DPICHANGED:
        m_dpi = static_cast<float>(LOWORD(wp));
        FitToPrimaryMonitor();
        Resize(); // rebuild the target bitmap at the new DPI
        return 0;

    case WM_DISPLAYCHANGE:
        FitToPrimaryMonitor();
        return 0;

    case WM_HOTKEY:
        if (wp == kQuitHotkeyId) {
            DestroyWindow(m_hwnd);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(m_hwnd, kTimerId);
        UnregisterHotKey(m_hwnd, kQuitHotkeyId);
        PostQuitMessage(0);
        return 0;

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
    // Resizing the window sends WM_SIZE, which resizes the swap chain.
    const RECT r = GetPrimaryMonitorRect();
    SetWindowPos(m_hwnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
        SWP_NOACTIVATE);
}