#pragma once
bool CreateDeviceResources()
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

bool CreateTargetBitmap()
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

void ReleaseDeviceResources()
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

void Resize()
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