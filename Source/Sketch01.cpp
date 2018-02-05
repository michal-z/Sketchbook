#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_3.h>
#pragma comment(lib, "d2d1.lib")


#define VHR(r) if (FAILED((r))) assert(0);
#define CRELEASE(c) if ((c)) { (c)->Release(); (c) = nullptr; }
#define k_Name "Sketch01"
#define k_ResolutionX 800
#define k_ResolutionY 800
#define k_SphereCount 500

struct Sphere
{
	D2D1_ELLIPSE shape;
	D2D1_COLOR_F color;
};

static double s_Time;
static float s_DeltaTime;
static ID2D1Factory7 *s_Factory;
static ID2D1HwndRenderTarget *s_RenderTarget;
static ID2D1SolidColorBrush *s_FillBrush;
static ID2D1SolidColorBrush *s_StrokeBrush;
static Sphere *s_Spheres;

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

static void Setup()
{
	VHR(s_RenderTarget->CreateSolidColorBrush(D2D1::ColorF(0), &s_FillBrush));
	VHR(s_RenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0.125f), &s_StrokeBrush));

	s_Spheres = new Sphere[k_SphereCount];
	for (uint32_t i = 0; i < k_SphereCount; ++i)
	{
		float x = Randomf(0.0f, k_ResolutionX);
		float y = Randomf(0.0f, k_ResolutionY);
		float r = Randomf(20.0f, 120.0f);

		s_Spheres[i].shape = D2D1::Ellipse(D2D1::Point2F(x, y), r, r);
		s_Spheres[i].color = D2D1::ColorF(Randomf(), Randomf(), Randomf(), 0.2f);
	}
}

static void Draw()
{
	s_RenderTarget->BeginDraw();
	s_RenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));

	for (int i = 0; i < k_SphereCount; ++i)
	{
		s_FillBrush->SetColor(s_Spheres[i].color);
		s_RenderTarget->FillEllipse(s_Spheres[i].shape, s_FillBrush);
		s_RenderTarget->DrawEllipse(s_Spheres[i].shape, s_StrokeBrush, 3.0f);
	}
	VHR(s_RenderTarget->EndDraw());
}

static double GetTime()
{
	static LARGE_INTEGER s_Counter0;
	static LARGE_INTEGER s_Frequency;
	if (s_Counter0.QuadPart == 0)
	{
		QueryPerformanceFrequency(&s_Frequency);
		QueryPerformanceCounter(&s_Counter0);
	}
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return (counter.QuadPart - s_Counter0.QuadPart) / (double)s_Frequency.QuadPart;
}

static void UpdateFrameTime(HWND window, double *time, float *deltaTime)
{
	static double s_LastTime = -1.0;
	static double s_LastFpsTime = 0.0;
	static unsigned s_FrameCount = 0;

	if (s_LastTime < 0.0)
	{
		s_LastTime = GetTime();
		s_LastFpsTime = s_LastTime;
	}

	*time = GetTime();
	*deltaTime = (float)(*time - s_LastTime);
	s_LastTime = *time;

	if ((*time - s_LastFpsTime) >= 1.0)
	{
		const double fps = s_FrameCount / (*time - s_LastFpsTime);
		const double ms = (1.0 / fps) * 1000.0;
		char text[256];
		snprintf(text, sizeof(text), "[%.1f fps  %.3f ms] %s", fps, ms, k_Name);
		SetWindowText(window, text);
		s_LastFpsTime = *time;
		s_FrameCount = 0;
	}
	s_FrameCount++;
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

	RECT rect = { 0, 0, k_ResolutionX, k_ResolutionY };
	if (!AdjustWindowRect(&rect, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX, 0))
		assert(0);

	HWND hwnd = CreateWindowEx(
		0, k_Name, k_Name, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		nullptr, nullptr, nullptr, 0);
	assert(hwnd);

#ifdef _DEBUG
	D2D1_FACTORY_OPTIONS options;
	options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
	VHR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, &s_Factory));
#else
	VHR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &s_Factory));
#endif

	D2D1_SIZE_U size = D2D1::SizeU(k_ResolutionX, k_ResolutionY);

	VHR(s_Factory->CreateHwndRenderTarget(
		D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96.0f, 96.0f),
		D2D1::HwndRenderTargetProperties(hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
		&s_RenderTarget));

	return hwnd;
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	SetProcessDPIAware();

	HWND hwnd = MakeWindow();
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
	CRELEASE(s_RenderTarget);
	CRELEASE(s_Factory);
	return 0;
}
