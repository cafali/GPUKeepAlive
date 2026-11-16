
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <thread>
#include <chrono>
#include <string>
#include <filesystem>

#include "resource.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

static constexpr UINT WM_TRAY = WM_APP + 1;
static constexpr UINT ID_TRAY = 1001;
static constexpr UINT ID_INFO = 2001;
static constexpr UINT ID_EXIT = 2002;

static constexpr UINT NVIDIA_VENDOR_ID = 0x10DE;

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_renderOk{false};

static NOTIFYICONDATAW g_nid{};
static HWND g_hwnd{};
static bool g_trayVisible = true;

static std::wstring g_gpuName = L"NVIDIA GPU not initialized";

static std::wstring AppDir()
{
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return std::filesystem::path(p).parent_path().wstring();
}

static std::wstring ReadmePath()
{
    return AppDir() + L"\\README.txt";
}

static bool TraySetting()
{
    const auto p = AppDir() + L"\\tray.ini";

    wchar_t value[16] = L"1";

    GetPrivateProfileStringW(
        L"General",
        L"Tray",
        L"1",
        value,
        16,
        p.c_str()
    );

    return wcstol(value, nullptr, 10) != 0;
}

static void SetStatusTip(const wchar_t* tip)
{
    wcsncpy_s(g_nid.szTip, tip, _TRUNCATE);
    g_nid.uFlags = NIF_TIP;

    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();

    if (!menu)
        return;

    AppendMenuW(menu, MF_STRING, ID_INFO, L"Info");
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);

    SetForegroundWindow(hwnd);

    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON,
        pt.x,
        pt.y,
        0,
        hwnd,
        nullptr
    );

    DestroyMenu(menu);
}

// --------------------------------------------------
// NVIDIA GPU ENUMERATION
// --------------------------------------------------

static bool FindNvidiaAdapter(
    ComPtr<IDXGIAdapter1>& nvidiaAdapter,
    DXGI_ADAPTER_DESC1& selectedDesc)
{
    ComPtr<IDXGIFactory1> factory;

    HRESULT hr = CreateDXGIFactory1(
        IID_PPV_ARGS(&factory)
    );

    if (FAILED(hr))
    {
        OutputDebugStringW(
            L"GPUKeepAlive: DXGI factory creation failed.\n"
        );

        return false;
    }

    for (UINT i = 0; ; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;

        hr = factory->EnumAdapters1(i, &adapter);

        if (hr == DXGI_ERROR_NOT_FOUND)
            break;

        if (FAILED(hr))
            return false;

        DXGI_ADAPTER_DESC1 desc{};

        if (FAILED(adapter->GetDesc1(&desc)))
            continue;

        // Ignore software rendering adapters.
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        wchar_t info[512]{};

        swprintf_s(
            info,
            L"GPUKeepAlive: Found GPU: %ls | Vendor: 0x%04X\n",
            desc.Description,
            desc.VendorId
        );

        OutputDebugStringW(info);

        // Select NVIDIA only.
        if (desc.VendorId == NVIDIA_VENDOR_ID)
        {
            nvidiaAdapter = adapter;
            selectedDesc = desc;

            OutputDebugStringW(
                L"GPUKeepAlive: NVIDIA adapter selected.\n"
            );

            return true;
        }
    }

    OutputDebugStringW(
        L"GPUKeepAlive: No NVIDIA GPU found.\n"
    );

    return false;
}

// --------------------------------------------------
// GPU INITIALIZATION
// --------------------------------------------------

static bool InitGpu()
{
    // Find NVIDIA adapter.
    ComPtr<IDXGIAdapter1> nvidiaAdapter;
    DXGI_ADAPTER_DESC1 adapterDesc{};

    if (!FindNvidiaAdapter(nvidiaAdapter, adapterDesc))
        return false;

    // Compile shaders.
    const char* shader =
        "struct V{float4 p:SV_POSITION;};"
        "V VS(uint id:SV_VertexID){"
        "V o;"
        "float2 p[3]={"
        "float2(-1,-1),"
        "float2(0,1),"
        "float2(1,-1)"
        "};"
        "o.p=float4(p[id],0,1);"
        "return o;"
        "}"
        "float4 PS():SV_TARGET{"
        "return float4(0.02,0.04,0.08,1);"
        "}";

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompile(
        shader,
        strlen(shader),
        nullptr,
        nullptr,
        nullptr,
        "VS",
        "vs_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &vsBlob,
        &errors
    );

    if (FAILED(hr))
        return false;

    errors.Reset();

    hr = D3DCompile(
        shader,
        strlen(shader),
        nullptr,
        nullptr,
        nullptr,
        "PS",
        "ps_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &psBlob,
        &errors
    );

    if (FAILED(hr))
        return false;

    UINT flags = 0;

#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL levels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    D3D_FEATURE_LEVEL got{};

    // IMPORTANT:
    // Explicit NVIDIA adapter selection.
    // D3D_DRIVER_TYPE_UNKNOWN is required when an adapter is supplied.

    hr = D3D11CreateDevice(
        nvidiaAdapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        &device,
        &got,
        &context
    );

    // Compatibility fallback for older D3D11 runtimes.
    if (hr == E_INVALIDARG)
    {
        hr = D3D11CreateDevice(
            nvidiaAdapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            levels + 1,
            ARRAYSIZE(levels) - 1,
            D3D11_SDK_VERSION,
            &device,
            &got,
            &context
        );
    }

    if (FAILED(hr))
    {
        OutputDebugStringW(
            L"GPUKeepAlive: NVIDIA D3D11 initialization failed.\n"
        );

        return false;
    }

    // --------------------------------------------------
    // VERIFY ACTUAL DEVICE ADAPTER
    // --------------------------------------------------

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> actualAdapter;

    DXGI_ADAPTER_DESC actualDesc{};

    if (FAILED(device.As(&dxgiDevice)))
        return false;

    if (FAILED(dxgiDevice->GetAdapter(&actualAdapter)))
        return false;

    if (FAILED(actualAdapter->GetDesc(&actualDesc)))
        return false;

    // Never continue if the actual device isn't NVIDIA.
    if (actualDesc.VendorId != NVIDIA_VENDOR_ID)
    {
        OutputDebugStringW(
            L"GPUKeepAlive: GPU verification failed!\n"
        );

        return false;
    }

    // Store GPU information.
    g_gpuName = actualDesc.Description;

    wchar_t gpuInfo[512]{};

    swprintf_s(
        gpuInfo,
        L"GPUKeepAlive ACTIVE\n"
        L"GPU: %ls\n"
        L"Vendor ID: 0x%04X\n"
        L"Device ID: 0x%04X\n"
        L"Dedicated VRAM: %llu MB\n"
        L"Feature Level: 0x%X\n",
        actualDesc.Description,
        actualDesc.VendorId,
        actualDesc.DeviceId,
        actualDesc.DedicatedVideoMemory / (1024 * 1024),
        static_cast<unsigned int>(got)
    );

    OutputDebugStringW(gpuInfo);

    // --------------------------------------------------
    // CREATE OFF-SCREEN RENDER TARGET
    // --------------------------------------------------

    D3D11_TEXTURE2D_DESC td{};

    td.Width = 64;
    td.Height = 64;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;

    ComPtr<ID3D11Texture2D> texture;

    if (FAILED(device->CreateTexture2D(
        &td,
        nullptr,
        &texture)))
        return false;

    ComPtr<ID3D11RenderTargetView> rtv;

    if (FAILED(device->CreateRenderTargetView(
        texture.Get(),
        nullptr,
        &rtv)))
        return false;

    // Create shaders.
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;

    if (FAILED(device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &vs)))
        return false;

    if (FAILED(device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &ps)))
        return false;

    // Configure rendering pipeline.
    D3D11_VIEWPORT vp{};

    vp.Width = 64.0f;
    vp.Height = 64.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    context->OMSetRenderTargets(
        1,
        rtv.GetAddressOf(),
        nullptr
    );

    context->RSSetViewports(1, &vp);

    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
    );

    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);

    // NVIDIA successfully initialized and verified.
    g_renderOk = true;

    // Update tray tooltip.
    if (g_trayVisible)
        SetStatusTip(L"GPUKeepAlive - NVIDIA active");

    OutputDebugStringW(
        L"GPUKeepAlive: NVIDIA rendering started.\n"
    );

    // --------------------------------------------------
    // GPU RENDERING LOOP
    // --------------------------------------------------

    while (g_running.load())
    {
        const float clearColor[4] =
        {
            0.01f,
            0.02f,
            0.03f,
            1.0f
        };

        context->ClearRenderTargetView(
            rtv.Get(),
            clearColor
        );

        context->Draw(3, 0);

        // Submit queued GPU commands.
        context->Flush();

        // Small workload interval.
        std::this_thread::sleep_for(
            std::chrono::seconds(120) 
        );
    }

    context->ClearState();

    g_renderOk = false;

    return true;
}

// --------------------------------------------------
// WINDOW PROCEDURE
// --------------------------------------------------

static LRESULT CALLBACK WndProc(
    HWND hwnd,
    UINT msg,
    WPARAM wp,
    LPARAM lp)
{
    if (msg == WM_TRAY)
    {
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU)
            ShowTrayMenu(hwnd);

        else if (lp == WM_LBUTTONDBLCLK)
        {
            ShellExecuteW(
                nullptr,
                L"open",
                ReadmePath().c_str(),
                nullptr,
                AppDir().c_str(),
                SW_SHOWNORMAL
            );
        }

        return 0;
    }

    if (msg == WM_COMMAND)
    {
        if (LOWORD(wp) == ID_INFO)
        {
            wchar_t info[512]{};

            swprintf_s(
                info,
                L"GPUKeepAlive\n\n"
                L"GPU: %ls\n"
                L"NVIDIA active: %ls",
                g_gpuName.c_str(),
                g_renderOk.load() ? L"YES" : L"NO"
            );

            MessageBoxW(
                hwnd,
                info,
                L"GPUKeepAlive Information",
                MB_OK | MB_ICONINFORMATION
            );
        }
        else if (LOWORD(wp) == ID_EXIT)
        {
            DestroyWindow(hwnd);
        }

        return 0;
    }

    if (msg == WM_DESTROY)
    {
        if (g_trayVisible)
            Shell_NotifyIconW(NIM_DELETE, &g_nid);

        g_running = false;

        PostQuitMessage(0);

        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --------------------------------------------------
// APPLICATION ENTRY POINT
// --------------------------------------------------

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    HANDLE mutex = CreateMutexW(
        nullptr,
        TRUE,
        L"Local\\GPUKeepAlive_SingleInstance"
    );

    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
            CloseHandle(mutex);

        return 0;
    }

    g_trayVisible = TraySetting();

    WNDCLASSW wc{};

    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"GPUKeepAliveHiddenWindow";
    wc.hIcon = LoadIconW(
        instance,
        MAKEINTRESOURCEW(IDI_APP)
    );

    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"GPUKeepAlive",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance,
        nullptr
    );

    if (!g_hwnd)
    {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 2;
    }

    if (g_trayVisible)
    {
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = g_hwnd;
        g_nid.uID = ID_TRAY;

        g_nid.uFlags =
            NIF_MESSAGE |
            NIF_ICON |
            NIF_TIP;

        g_nid.uCallbackMessage = WM_TRAY;

        g_nid.hIcon = (HICON)LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_APP),
            IMAGE_ICON,
            0,
            0,
            LR_DEFAULTSIZE
        );

        wcscpy_s(
            g_nid.szTip,
            L"GPUKeepAlive - Initializing"
        );

        Shell_NotifyIconW(NIM_ADD, &g_nid);
    }

    // Initialize NVIDIA GPU on a separate thread.
    std::thread gpu([]()
    {
        if (!InitGpu())
        {
            g_renderOk = false;

            if (g_running.load() && g_hwnd)
            {
                MessageBoxW(
                    g_hwnd,
                    L"NVIDIA GPU initialization failed.\n\n"
                    L"No NVIDIA GPU was selected or Direct3D 11 "
                    L"initialization failed.\n\n"
                    L"Check your NVIDIA driver.",
                    L"GPUKeepAlive - Error",
                    MB_ICONWARNING | MB_OK
                );
            }
        }
    });

    MSG msg{};

    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_running = false;

    if (gpu.joinable())
        gpu.join();

    if (g_nid.hIcon)
        DestroyIcon(g_nid.hIcon);

    ReleaseMutex(mutex);
    CloseHandle(mutex);

    return 0;
}
