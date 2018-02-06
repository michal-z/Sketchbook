// D3D12 and D2D interop, based on:
// https://msdn.microsoft.com/en-us/library/windows/desktop/mt186590(v=vs.85).aspx
// TODO: Render UI to offscreen MS render target
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_4.h>
#include <d2d1_3.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include "d3dx12.h"
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")


#ifdef _DEBUG
#define ENABLE_DEBUG_LAYER
#endif

#define VHR(c) if (FAILED((c))) assert(0);
#define CRELEASE(c) if ((c)) { (c)->Release(); (c) = nullptr; }

#define k_DemoName "Direct3D 12 and Direct2D interop"
#define k_DemoResolutionX 1280
#define k_DemoResolutionY 720

// D3D12
ID3D12Device *s_Device;
ID3D12CommandQueue *s_CmdQueue;
ID3D12CommandAllocator *s_CmdAlloc[2];
ID3D12GraphicsCommandList *s_CmdList;
IDXGIFactory4 *s_D3d12Factory;
IDXGISwapChain3 *s_SwapChain;
ID3D12DescriptorHeap *s_SwapBuffersHeap;
ID3D12DescriptorHeap *s_DepthBufferHeap;
ID3D12Resource *s_SwapBuffers[4];
ID3D12Resource *s_DepthBuffer;
uint64_t s_FrameCount;
ID3D12Fence *s_FrameFence;
HANDLE s_FrameFenceEvent;
uint32_t s_DescriptorSize;
uint32_t s_DescriptorSizeRtv;
D3D12_CPU_DESCRIPTOR_HANDLE s_SwapBuffersHeapStart;
D3D12_CPU_DESCRIPTOR_HANDLE s_DepthBufferHeapStart;
// D3D11on12
ID3D11On12Device *s_D3d11On12Device;
ID3D11DeviceContext *s_D3d11DeviceContext;
ID3D11Resource *s_D3d11SwapBuffers[4];
// D2D1
ID2D1Factory6 *s_D2d1Factory;
ID2D1Device *s_D2d1Device;
ID2D1DeviceContext *s_D2d1DeviceContext;
ID2D1Bitmap1 *s_D2d1RenderTargets[4];
ID2D1SolidColorBrush *s_Brush;

// returns [0.0f, 1.0f)
inline float Randomf()
{
	const uint32_t exponent = 127;
	const uint32_t significand = (uint32_t)(rand() & 0x7fff); // get 15 random bits
	const uint32_t result = (exponent << 23) | (significand << 8);
	return *(float*)&result - 1.0f;
}

// returns [begin, end)
inline float Randomf(float begin, float end)
{
	assert(begin < end);
	return begin + (end - begin) * Randomf();
}

static double GetTime()
{
	static LARGE_INTEGER startCounter;
	static LARGE_INTEGER frequency;
	if (startCounter.QuadPart == 0)
	{
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&startCounter);
	}
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return (counter.QuadPart - startCounter.QuadPart) / (double)frequency.QuadPart;
}

static void UpdateFrameTime(HWND window, double *time, float *deltaTime)
{
	static double lastTime = -1.0;
	static double lastFpsTime = 0.0;
	static unsigned frameCount = 0;

	if (lastTime < 0.0)
	{
		lastTime = GetTime();
		lastFpsTime = lastTime;
	}

	*time = GetTime();
	*deltaTime = (float)(*time - lastTime);
	lastTime = *time;

	if ((*time - lastFpsTime) >= 1.0)
	{
		const double fps = frameCount / (*time - lastFpsTime);
		const double ms = (1.0 / fps) * 1000.0;
		char text[256];
		snprintf(text, sizeof(text), "[%.1f fps  %.3f ms] %s", fps, ms, k_DemoName);
		SetWindowText(window, text);
		lastFpsTime = *time;
		frameCount = 0;
	}
	frameCount++;
}

static LRESULT CALLBACK ProcessWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_KEYDOWN:
		if (wparam == VK_ESCAPE)
		{
			PostQuitMessage(0);
			return 0;
		}
		break;
	}
	return DefWindowProc(window, message, wparam, lparam);
}

static HWND MakeWindow()
{
	WNDCLASS winclass = {};
	winclass.lpfnWndProc = ProcessWindowMessage;
	winclass.hInstance = GetModuleHandle(nullptr);
	winclass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	winclass.lpszClassName = k_DemoName;
	if (!RegisterClass(&winclass))
		assert(0);

	RECT rect = { 0, 0, k_DemoResolutionX, k_DemoResolutionY };
	if (!AdjustWindowRect(&rect, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX, 0))
		assert(0);

	HWND hwnd = CreateWindowEx(
		0, k_DemoName, k_DemoName, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		nullptr, nullptr, nullptr, 0);
	assert(hwnd);

#ifdef ENABLE_DEBUG_LAYER
	VHR(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&s_D3d12Factory)));
#else
	VHR(CreateDXGIFactory2(0, IID_PPV_ARGS(&s_D3d12Factory)));
#endif

#ifdef ENABLE_DEBUG_LAYER
	{
		ID3D12Debug *dbg = nullptr;
		D3D12GetDebugInterface(IID_PPV_ARGS(&dbg));
		if (dbg)
		{
			dbg->EnableDebugLayer();
			ID3D12Debug1 *dbg1;
			dbg->QueryInterface(IID_PPV_ARGS(&dbg1));
			if (dbg1)
				dbg1->SetEnableGPUBasedValidation(TRUE);
			CRELEASE(dbg);
			CRELEASE(dbg1);
		}
	}
#endif
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_1, IID_PPV_ARGS(&s_Device))))
		return false;

	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	VHR(s_Device->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&s_CmdQueue)));

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = 4;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = hwnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.Windowed = 1;

	IDXGISwapChain *tempSwapChain;
	VHR(s_D3d12Factory->CreateSwapChain(s_CmdQueue, &swapChainDesc, &tempSwapChain));
	VHR(tempSwapChain->QueryInterface(IID_PPV_ARGS(&s_SwapChain)));
	CRELEASE(tempSwapChain);

	for (uint32_t i = 0; i < 2; ++i)
		VHR(s_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s_CmdAlloc[i])));

	s_DescriptorSize = s_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	s_DescriptorSizeRtv = s_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	/* D2D1 */ {
		ID3D11Device *d3d11Device;
		VHR(D3D11On12CreateDevice(
			s_Device, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, (IUnknown **)&s_CmdQueue, 1, 1,
			&d3d11Device, &s_D3d11DeviceContext, nullptr));

		VHR(d3d11Device->QueryInterface(&s_D3d11On12Device));
		CRELEASE(d3d11Device);

		D2D1_FACTORY_OPTIONS factoryOptions;
#ifdef ENABLE_DEBUG_LAYER
		factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#else
		factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
		VHR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory6), &factoryOptions, (void **)&s_D2d1Factory));

		IDXGIDevice *dxgiDevice;
		VHR(s_D3d11On12Device->QueryInterface(&dxgiDevice));
		VHR(s_D2d1Factory->CreateDevice(dxgiDevice, &s_D2d1Device));

		D2D1_DEVICE_CONTEXT_OPTIONS deviceOptions = D2D1_DEVICE_CONTEXT_OPTIONS_NONE;
		VHR(s_D2d1Device->CreateDeviceContext(deviceOptions, &s_D2d1DeviceContext));

		CRELEASE(dxgiDevice);
	}

	/* swap buffers */ {
		D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
			96.0f, 96.0f);

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 4;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VHR(s_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_SwapBuffersHeap)));
		s_SwapBuffersHeapStart = s_SwapBuffersHeap->GetCPUDescriptorHandleForHeapStart();

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(s_SwapBuffersHeapStart);

		for (uint32_t i = 0; i < 4; ++i)
		{
			VHR(s_SwapChain->GetBuffer(i, IID_PPV_ARGS(&s_SwapBuffers[i])));
			s_Device->CreateRenderTargetView(s_SwapBuffers[i], nullptr, handle);
			handle.Offset(s_DescriptorSizeRtv);

			D3D11_RESOURCE_FLAGS d3d11Flags = { D3D11_BIND_RENDER_TARGET };
			VHR(s_D3d11On12Device->CreateWrappedResource(
				//s_SwapBuffers[i], &d3d11Flags, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT,
				s_SwapBuffers[i], &d3d11Flags, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT,
				IID_PPV_ARGS(&s_D3d11SwapBuffers[i])));

			IDXGISurface *surface;
			s_D3d11SwapBuffers[i]->QueryInterface(&surface);
			VHR(s_D2d1DeviceContext->CreateBitmapFromDxgiSurface(surface, &bitmapProperties, &s_D2d1RenderTargets[i]));
			CRELEASE(surface);
		}

		VHR(s_D2d1DeviceContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.5f, 0.0f, 1.0f), &s_Brush));
	}
#if 0
	{	// depth buffer
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VHR(s_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_DepthBufferHeap)));
		m_DepthBufferHeapStart = m_DepthBufferHeap->GetCPUDescriptorHandleForHeapStart();

		CD3DX12_RESOURCE_DESC imageDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_ScissorRect.right, m_ScissorRect.bottom);
		imageDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		VHR(s_Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
			&imageDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.0f, 0), IID_PPV_ARGS(&m_DepthBuffer)));

		D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
		viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
		viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		viewDesc.Flags = D3D12_DSV_FLAG_NONE;
		s_Device->CreateDepthStencilView(m_DepthBuffer, &viewDesc, m_DepthBufferHeapStart);
	}
#endif

	return hwnd;
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	SetProcessDPIAware();

	HWND hwnd = MakeWindow();

	for (;;)
	{
		MSG msg = {};
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				break;
		}
		else
		{
			double time;
			float deltaTime;
			UpdateFrameTime(hwnd, &time, &deltaTime);

			uint32_t bufferIndex = s_SwapChain->GetCurrentBackBufferIndex();

			s_D3d11On12Device->AcquireWrappedResources(&s_D3d11SwapBuffers[bufferIndex], 1);


			s_D2d1DeviceContext->SetTarget(s_D2d1RenderTargets[bufferIndex]);
			s_D2d1DeviceContext->BeginDraw();
			s_D2d1DeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());

			s_D2d1DeviceContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(k_DemoResolutionX/2, k_DemoResolutionY/2), 100.0f, 150.0f), s_Brush);

			VHR(s_D2d1DeviceContext->EndDraw());

			s_D3d11On12Device->ReleaseWrappedResources(&s_D3d11SwapBuffers[bufferIndex], 1);

			s_D3d11DeviceContext->Flush();

			s_SwapChain->Present(0, 0);
		}
	}

	return 0;
}
