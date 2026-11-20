#include "Renderer.h"
#include "Renderer.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

Renderer::~Renderer()
{
    ReleaseDevDepResources();
}

bool Renderer::Initialize(HWND hwnd, UINT width, UINT height, float DPI)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
    m_DPI = DPI;

    if (!CreateDevIndResources()) {
        return false;
    }
    CreateDevDepResources();
    return true;
}

bool Renderer::CreateDevIndResources()
{
    D2D1_FACTORY_OPTIONS options{};
#ifdef _DEBUG
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    return SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options,
        m_d2dFactory.GetAddressOf()));
}

bool Renderer::CreateDevDepResources()
{
    if (!m_hwnd || m_width == 0 || m_height == 0 || !m_d2dFactory) {
        return false;
    }

    ReleaseDevDepResources();

    const bool ok = [&] {
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

        ComPtr<ID2D1Device> d2dDevice;
        if (FAILED(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice)) ||
            FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_dc))) {
            return false;
        }
        if (!CreateTargetBitmap()) {
            return false;
        }

        if (FAILED(m_dc->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f), &m_fillBrush)) ||
            FAILED(m_dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &m_borderBrush))) {
            return false;
        }

        return SUCCEEDED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_dcompDevice))) &&
            SUCCEEDED(m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget)) &&
            SUCCEEDED(m_dcompDevice->CreateVisual(&m_dcompVisual)) &&
            SUCCEEDED(m_dcompVisual->SetContent(m_swapChain.Get())) &&
            SUCCEEDED(m_dcompTarget->SetRoot(m_dcompVisual.Get())) &&
            SUCCEEDED(m_dcompDevice->Commit());
    }();

    if (!ok) {
        ReleaseDevDepResources();
    }
    return ok;
}

bool Renderer::CreateTargetBitmap()
{
    ComPtr<IDXGISurface> surface;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)))) {
        return false;
    }
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        m_DPI, m_DPI);
    if (FAILED(m_dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &m_targetBitmap))) {
        return false;
    }
    m_dc->SetTarget(m_targetBitmap.Get());
    m_dc->SetDpi(m_DPI, m_DPI);
    return true;
}

void Renderer::ReleaseDevDepResources()
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

void Renderer::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0) {
        return;
    }
    m_width = width;
    m_height = height;
    if (!m_dc) {
        return;
    }

    m_dc->SetTarget(nullptr);
    m_targetBitmap.Reset();
    if (FAILED(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)) ||
        !CreateTargetBitmap()) {
        ReleaseDevDepResources();
    }
}

void Renderer::SetDPI(float DPI)
{
    if (DPI == m_DPI) {
        return;
    }
    m_DPI = DPI;
    if (!m_dc) {
        return;
    }
    m_dc->SetTarget(nullptr);
    m_targetBitmap.Reset();
    if (!CreateTargetBitmap()) {
        ReleaseDevDepResources();
    }
}

bool Renderer::Render(const D2D1_RECT_F* scene)
{
    if (!m_dc && !CreateDevDepResources()) {
        return false;
    }

    m_dc->BeginDraw();
    m_dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    if (scene) {
        m_dc->FillRectangle(*scene, m_fillBrush.Get());
        m_dc->DrawRectangle(*scene, m_borderBrush.Get(), 2.0f);
    }

    HRESULT hr = m_dc->EndDraw();
    if (FAILED(hr)) {
        ReleaseDevDepResources();
        return false;
    }

    hr = m_swapChain->Present(1, 0);
    if (hr == DXGI_STATUS_OCCLUDED) {
        Sleep(50);
    }
    else if (FAILED(hr)) {
        ReleaseDevDepResources();
        return false;
    }
    return true;
}
