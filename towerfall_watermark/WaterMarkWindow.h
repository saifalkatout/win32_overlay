#pragma once
#include <windows.h>
#include <wrl/client.h>   // ComPtr: a smart pointer for COM objects (like shared_ptr)

#include <d2d1_1.h>       // Direct2D: 2D drawing
#include <d3d11.h>        // Direct3D 11: the GPU device Direct2D draws with
#include <dcomp.h>        // DirectComposition: hands our transparent image to Windows
#include <dwrite.h>       // DirectWrite: fonts and text
#include <dxgi1_2.h>      // DXGI: swap chain (the buffers we draw into)

#include <chrono>
#include <string>

// A click-through, always-on-top window covering the primary monitor.
// Draws a semi-transparent square with the watermark text that bounces
// around the screen.
class WatermarkWindow {
public:
    WatermarkWindow() = default;
    ~WatermarkWindow();

    WatermarkWindow(const WatermarkWindow&) = delete;
    WatermarkWindow& operator=(const WatermarkWindow&) = delete;

    bool Create(HINSTANCE instance);

    // Draws one frame, then waits for the monitor's next refresh.
    void Render();

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    bool CreateDeviceResources();   // GPU objects; rebuilt if the GPU resets
    void ReleaseDeviceResources();
    bool CreateTargetBitmap();      // depends on window size and DPI
    void Resize();

    void UpdateText();              // rebuilds the text layout when the minute changes
    void UpdateAnimation(float seconds);
    void FitToPrimaryMonitor();

    HWND  m_hwnd = nullptr;
    UINT  m_width = 0;             // physical pixels
    UINT  m_height = 0;
    float m_dpi = 96.0f;

    // Created once, not tied to the GPU.
    ComPtr<ID2D1Factory1>     m_d2dFactory;

    // Tied to the GPU: thrown away and recreated if the device is lost.
    ComPtr<ID3D11Device>         m_d3dDevice;
    ComPtr<IDXGISwapChain1>      m_swapChain;
    ComPtr<ID2D1DeviceContext>   m_dc;
    ComPtr<ID2D1Bitmap1>         m_targetBitmap;
    ComPtr<ID2D1SolidColorBrush> m_fillBrush;
    ComPtr<ID2D1SolidColorBrush> m_borderBrush;
    ComPtr<IDCompositionDevice>  m_dcompDevice;
    ComPtr<IDCompositionTarget>  m_dcompTarget;
    ComPtr<IDCompositionVisual>  m_dcompVisual;

    // The watermark square and its motion, in DIPs (1 DIP = 1 pixel at 100% scaling).
    float         m_side = 100.0f;               // the square's width and height
    float    kCorner = 1.0f;        // corner radius
    D2D1_POINT_2F m_pos{ 100.0f, 100.0f };     // top-left corner
    D2D1_POINT_2F m_vel{ 140.0f, 100.0f };     // DIPs per second
    std::chrono::steady_clock::time_point m_lastFrame{};
};
