#pragma once
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>

class Renderer {
public:
	Renderer() = default;
	~Renderer();
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	bool Initialize(HWND hwnd, UINT width, UINT height, float DPI);
	void Resize(UINT width, UINT height);
	void SetDPI(float DPI);

	bool Render(const D2D1_RECT_F* scene);

private:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	bool CreateDevIndResources();
	bool CreateDevDepResources();
	bool CreateTargetBitmap();
	void ReleaseDevDepResources();

	HWND m_hwnd = nullptr;
	UINT m_width = 0, m_height = 0;
	float m_DPI = 96.0f;

	ComPtr<ID2D1Factory1>        m_d2dFactory;
	ComPtr<ID3D11Device>         m_d3dDevice;
	ComPtr<IDXGISwapChain1>      m_swapChain;
	ComPtr<ID2D1DeviceContext>   m_dc;
	ComPtr<ID2D1Bitmap1>         m_targetBitmap;
	ComPtr<ID2D1SolidColorBrush> m_fillBrush;
	ComPtr<ID2D1SolidColorBrush> m_borderBrush;
	ComPtr<IDCompositionDevice>  m_dcompDevice;
	ComPtr<IDCompositionTarget>  m_dcompTarget;
	ComPtr<IDCompositionVisual>  m_dcompVisual;
};
