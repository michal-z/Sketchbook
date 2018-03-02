#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_2.h>
#include <d2d1_3.h>
#include <d3d11.h>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
using namespace D2D1;


#ifdef _DEBUG
#define ENABLE_DEBUG_LAYER
#endif
#define VHR(r) if (FAILED((r))) assert(0);
#define CRELEASE(c) if ((c)) { (c)->Release(); (c) = nullptr; }

struct Sphere
{
	D2D1_ELLIPSE shape;
	D2D1_COLOR_F color;
};

static const char *k_Name = "Sketch02";
static const unsigned k_ResolutionX = 800;
static const unsigned k_ResolutionY = 800;
static const unsigned k_SphereCount = 500;
static const unsigned k_ScreenX = GetSystemMetrics(SM_CXSCREEN);
static const unsigned k_ScreenY = GetSystemMetrics(SM_CYSCREEN);
static double s_Time;
static float s_DeltaTime;
static ID2D1Device6 *s_D2dDevice;
static ID2D1DeviceContext6 *s_D2dContext;
static ID2D1Factory7 *s_D2dFactory;
static ID2D1Bitmap1 *s_D2dTargetBitmap;
static IDXGISwapChain1 *s_SwapChain;
static ID2D1SolidColorBrush *s_FillBrush;
static ID2D1SolidColorBrush *s_StrokeBrush;
static Sphere *s_Spheres;

// returns [0.0f, 1.0f)
static inline float Randomf()
{
	const uint32_t exponent = 127;
	const uint32_t significand = (uint32_t)(rand() & 0x7fff); // get 15 random bits
	const uint32_t result = (exponent << 23) | (significand << 8);
	return *(float *)&result - 1.0f;
}

// returns [begin, end)
static inline float Randomf(float begin, float end)
{
	assert(begin < end);
	return begin + (end - begin) * Randomf();
}

static void Setup()
{
	VHR(s_D2dContext->CreateSolidColorBrush(ColorF(0), &s_FillBrush));
	VHR(s_D2dContext->CreateSolidColorBrush(ColorF(0, 0.125f), &s_StrokeBrush));

	s_Spheres = new Sphere[k_SphereCount];
	for (uint32_t i = 0; i < k_SphereCount; ++i)
	{
		float x = Randomf((float)(k_ScreenX / 2 - k_ResolutionX / 2), (float)(k_ScreenX / 2 + k_ResolutionX / 2));
		float y = Randomf((float)(k_ScreenY / 2 - k_ResolutionY / 2), (float)(k_ScreenY / 2 + k_ResolutionY / 2));
		float r = Randomf(20.0f, 120.0f);

		s_Spheres[i].shape = Ellipse(Point2F(x, y), r, r);
		s_Spheres[i].color = ColorF(Randomf(), Randomf(), Randomf(), 0.2f);
	}
}

static void Draw()
{
	s_D2dContext->BeginDraw();
	s_D2dContext->Clear(ColorF(ColorF::White));

	s_D2dContext->PushAxisAlignedClip(RectF(
		(float)(k_ScreenX / 2 - k_ResolutionX / 2),
		(float)(k_ScreenY / 2 - k_ResolutionY / 2),
		(float)(k_ScreenX / 2 + k_ResolutionX / 2),
		(float)(k_ScreenY / 2 + k_ResolutionY / 2)), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

	for (int i = 0; i < k_SphereCount; ++i)
	{
		s_FillBrush->SetColor(s_Spheres[i].color);
		s_D2dContext->FillEllipse(s_Spheres[i].shape, s_FillBrush);
		s_D2dContext->DrawEllipse(s_Spheres[i].shape, s_StrokeBrush, 3.0f);
	}

	s_D2dContext->PopAxisAlignedClip();

	VHR(s_D2dContext->EndDraw());
	VHR(s_SwapChain->Present(0, 0));
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
		snprintf(text, sizeof(text), "[%.1f fps  %.3f ms] %s", fps, ms, k_Name);
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
	winclass.lpszClassName = k_Name;
	if (!RegisterClass(&winclass))
		assert(0);

	HWND hwnd = CreateWindowEx(
		0, k_Name, k_Name, WS_POPUP | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, k_ScreenX, k_ScreenY,
		nullptr, nullptr, nullptr, 0);
	assert(hwnd);

	D2D1_FACTORY_OPTIONS factoryOptions;
#ifdef ENABLE_DEBUG_LAYER
	factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#else
	factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
	VHR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(s_D2dFactory), &factoryOptions, (void **)&s_D2dFactory));

	ID3D11Device *d3d11Device;
	ID3D11DeviceContext *d3d11Context;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
	VHR(D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		&featureLevel, 1, D3D11_SDK_VERSION, &d3d11Device, nullptr, &d3d11Context));

	IDXGIDevice *dxgiDevice;
	VHR(d3d11Device->QueryInterface(&dxgiDevice));

	VHR(s_D2dFactory->CreateDevice(dxgiDevice, &s_D2dDevice));
	VHR(s_D2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &s_D2dContext));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Scaling = DXGI_SCALING_NONE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

	IDXGIAdapter *dxgiAdapter;
	IDXGIFactory2 *dxgiFactory;
	VHR(dxgiDevice->GetAdapter(&dxgiAdapter));
	VHR(dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));
	VHR(dxgiFactory->CreateSwapChainForHwnd(d3d11Device, hwnd, &swapChainDesc, nullptr, nullptr, &s_SwapChain));

	D2D1_BITMAP_PROPERTIES1 bitmapProperties = BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);

	IDXGISurface *dxgiBackBuffer;
	VHR(s_SwapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer)));

	VHR(s_D2dContext->CreateBitmapFromDxgiSurface(dxgiBackBuffer, &bitmapProperties, &s_D2dTargetBitmap));
	s_D2dContext->SetTarget(s_D2dTargetBitmap);

	CRELEASE(d3d11Device);
	CRELEASE(d3d11Context);
	CRELEASE(dxgiBackBuffer);
	CRELEASE(dxgiDevice);
	CRELEASE(dxgiAdapter);
	CRELEASE(dxgiFactory);

	return hwnd;
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	HWND hwnd = MakeWindow();
	ShowCursor(FALSE);
	Setup();

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
			UpdateFrameTime(hwnd, &s_Time, &s_DeltaTime);
			Draw();
		}
	}

	delete[] s_Spheres;
	CRELEASE(s_FillBrush);
	CRELEASE(s_StrokeBrush);
	CRELEASE(s_D2dFactory);
	CRELEASE(s_D2dContext);
	CRELEASE(s_D2dDevice);
	CRELEASE(s_D2dTargetBitmap);
	CRELEASE(s_SwapChain);
	return 0;
}
