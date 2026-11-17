#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "shell32.lib")

#include "resource.h"

using Microsoft::WRL::ComPtr;

static constexpr UINT WM_TRAY = WM_APP + 1;
static constexpr UINT ID_TRAY = 1001;
static constexpr UINT ID_ACTIVE_GPU = 2001;
static constexpr UINT ID_CFG_FOLDER = 2002;
static constexpr UINT ID_CHECK_UPDATES = 2004;
static constexpr UINT ID_VISIT_GITHUB = 2005;
static constexpr UINT ID_HIDE_TRAY = 2006;
static constexpr UINT ID_RESTART = 2007;
static constexpr UINT ID_EXIT = 2008;
static constexpr UINT ID_VERSION_INFO = 2009;

static constexpr UINT ID_INTERVAL_30 = 2101;
static constexpr UINT ID_INTERVAL_60 = 2102;
static constexpr UINT ID_INTERVAL_120 = 2103;
static constexpr UINT ID_INTERVAL_300 = 2104;
static constexpr UINT ID_INTERVAL_600 = 2105;
static constexpr UINT ID_INTERVAL_CUSTOM = 2106;
static constexpr UINT ID_INTERVAL_15 = 2107;
static constexpr UINT ID_INTERVAL_1800 = 2108;
static constexpr UINT ID_INTERVAL_3600 = 2109;

static constexpr UINT NVIDIA_VENDOR_ID = 0x10DE;
static constexpr int DEFAULT_INTERVAL_SECONDS = 120;
static constexpr wchar_t APP_VERSION[] = L"1.0.2";

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_renderOk{false};
static std::atomic<int> g_gpuIntervalSeconds{DEFAULT_INTERVAL_SECONDS};
static std::atomic<bool> g_trayVisible{true};
static std::atomic<bool> g_restartRequested{false};
static std::atomic<bool> g_trayRegistered{false};
static std::atomic<bool> g_recoveryThreadActive{false};

static NOTIFYICONDATAW g_nid{};
static HWND g_hwnd{};
static std::wstring g_gpuName = L"NVIDIA GPU not initialized";
static std::wstring g_gpuIdText = L"NVIDIA GPU not initialized";
static UINT g_taskbarCreatedMsg = 0;

static void RestartApplication();

static std::wstring AppDir()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().wstring();
}

static std::wstring ReadmePath()
{
    return AppDir() + L"\\README.md";
}

static std::wstring LocalAppDataDir()
{
    PWSTR knownPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &knownPath)))
    {
        std::wstring path = knownPath;
        CoTaskMemFree(knownPath);
        return path;
    }

    wchar_t* envValue = nullptr;
    size_t envLen = 0;
    _wdupenv_s(&envValue, &envLen, L"LOCALAPPDATA");
    if (envValue)
    {
        std::wstring path = envValue;
        free(envValue);
        return path;
    }

    return AppDir();
}

static std::wstring ConfigDir()
{
    return LocalAppDataDir() + L"\\GPUKeepAlive";
}

static std::wstring ConfigPath(const wchar_t* fileName)
{
    return ConfigDir() + L"\\" + fileName;
}

static bool FileExists(const std::wstring& path)
{
    return std::filesystem::exists(path);
}

static void EnsureConfigDirectory()
{
    std::error_code ec;
    std::filesystem::create_directories(ConfigDir(), ec);
}

static void WriteTextFile(const std::wstring& path, const std::wstring& content)
{
    std::wofstream stream(path.c_str(), std::ios::out | std::ios::trunc);
    if (stream.is_open())
        stream << content;
}

static std::wstring TrimString(const std::wstring& value)
{
    const auto start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos)
        return L"";

    const auto end = value.find_last_not_of(L" \t\r\n");
    return value.substr(start, end - start + 1);
}

static std::wstring ExtractValueAfterEquals(const std::wstring& line)
{
    const auto pos = line.find(L'=' );
    if (pos == std::wstring::npos)
        return L"";

    return TrimString(line.substr(pos + 1));
}

static void EnsureDefaultTrayConfig()
{
    EnsureConfigDirectory();
    const std::wstring path = ConfigPath(L"tray.ini");
    if (FileExists(path))
        return;

    const std::wstring content =
        L"[General]\n"
        L"Tray=1\n";

    WriteTextFile(path, content);
}

static void EnsureDefaultIntervalConfig()
{
    EnsureConfigDirectory();
    const std::wstring path = ConfigPath(L"interval.cfg");
    if (FileExists(path))
        return;

    const std::wstring content =
        L"; GPUKeepAlive refresh interval\n"
        L"; Presets: 15, 30, 60, 120, 300, 600, 1800, 3600 seconds; any positive number is also allowed.\n"
        L"; Default: 120\n"
        L"interval=120\n";

    WriteTextFile(path, content);
}

static bool TraySetting()
{
    EnsureConfigDirectory();
    EnsureDefaultTrayConfig();

    const std::wstring path = ConfigPath(L"tray.ini");
    wchar_t value[16] = L"1";
    GetPrivateProfileStringW(L"General", L"Tray", L"1", value, 16, path.c_str());
    return wcstol(value, nullptr, 10) != 0;
}

static void SaveTraySetting(bool visible)
{
    EnsureConfigDirectory();
    const std::wstring path = ConfigPath(L"tray.ini");
    const std::wstring content = L"[General]\nTray=" + std::wstring(visible ? L"1" : L"0") + L"\n";
    WriteTextFile(path, content);
    g_trayVisible.store(visible);
}

static int ReadIntervalSetting()
{
    EnsureConfigDirectory();
    EnsureDefaultIntervalConfig();

    const std::wstring intervalPath = ConfigPath(L"interval.cfg");
    std::wifstream stream(intervalPath.c_str());
    if (!stream.is_open())
        return DEFAULT_INTERVAL_SECONDS;

    std::wstring line;
    while (std::getline(stream, line))
    {
        std::wstring trimmed = TrimString(line);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#')
            continue;

        if (trimmed.find(L'=') != std::wstring::npos)
            trimmed = ExtractValueAfterEquals(trimmed);

        if (trimmed.empty())
            continue;

        wchar_t* end = nullptr;
        long value = std::wcstol(trimmed.c_str(), &end, 10);
        if (end != trimmed.c_str() && value > 0)
            return static_cast<int>(value);
    }

    return DEFAULT_INTERVAL_SECONDS;
}

static void SaveIntervalSetting(int seconds)
{
    const int safeSeconds = std::max(1, seconds);
    EnsureConfigDirectory();

    const std::wstring path = ConfigPath(L"interval.cfg");
    const std::wstring content =
        L"; GPUKeepAlive refresh interval\n"
        L"; Presets: 15, 30, 60, 120, 300, 600, 1800, 3600 seconds; any positive number is also allowed.\n"
        L"; Default: 120\n"
        L"interval=" + std::to_wstring(static_cast<long long>(safeSeconds)) + L"\n";

    WriteTextFile(path, content);
    g_gpuIntervalSeconds.store(safeSeconds);
}

static void UpdateHardwareFile()
{
    EnsureConfigDirectory();
    const std::wstring hardwareFile = ConfigPath(L"hardware.txt");
    std::wstring hardwareText = L"GPU: " + g_gpuName + L"\n";
    hardwareText += L"GPU Identifier: " + g_gpuIdText + L"\n";
    hardwareText += L"GPUKeepAlive active\n";

    WriteTextFile(hardwareFile, hardwareText);

    std::error_code ec;
    std::filesystem::remove(ConfigPath(L"GPUid.txt"), ec);
}

static void OpenConfigFolder()
{
    EnsureConfigDirectory();
    ShellExecuteW(nullptr, L"open", ConfigDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void OpenFileWithDefaultApp(const std::wstring& path)
{
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static bool IsTrayIconVisible()
{
    return g_trayVisible.load();
}

static bool LaunchCurrentExecutable()
{
    wchar_t exePath[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return false;

    std::wstring cmd = L"\"" + std::wstring(exePath) + L"\"";
    std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
    buffer.push_back(0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (!ok)
        return false;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static bool RegisterTrayIcon(HINSTANCE instance)
{
    if (!IsTrayIconVisible() || !g_hwnd)
        return false;

    if (g_nid.hIcon)
    {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = nullptr;
    }

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = ID_TRAY;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

    if (!g_nid.hIcon)
    {
        g_trayRegistered.store(false);
        return false;
    }

    wcscpy_s(g_nid.szTip, L"GPUKeepAlive - Active");
    if (!Shell_NotifyIconW(NIM_ADD, &g_nid))
    {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = nullptr;
        g_trayRegistered.store(false);
        return false;
    }

    g_trayRegistered.store(true);
    return true;
}

static void StartTrayRecoveryWorker(HINSTANCE instance)
{
    if (!IsTrayIconVisible() || g_recoveryThreadActive.exchange(true))
        return;

    std::thread([instance]()
    {
        while (g_running.load() && IsTrayIconVisible() && !g_trayRegistered.load())
        {
            if (RegisterTrayIcon(instance))
                break;

            std::this_thread::sleep_for(std::chrono::seconds(30));
        }

        g_recoveryThreadActive.store(false);
    }).detach();
}

static void SetStatusTip(const wchar_t* tip)
{
    if (!g_trayRegistered.load())
        return;

    wcsncpy_s(g_nid.szTip, tip, _TRUNCATE);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static bool RestoreTrayIconIfNeeded()
{
    if (!IsTrayIconVisible())
        return false;

    if (g_trayRegistered.load())
        return true;

    return RegisterTrayIcon(GetModuleHandleW(nullptr));
}

static void HideTrayIcon()
{
    if (!g_trayVisible.load())
        return;

    SaveTraySetting(false);
    RestartApplication();
}

static void OpenIntervalConfigFile()
{
    EnsureConfigDirectory();
    EnsureDefaultIntervalConfig();
    const std::wstring path = ConfigPath(L"interval.cfg");
    OpenFileWithDefaultApp(path);
}

static void ShowActiveGpuMessage(HWND hwnd)
{
    const std::wstring info = L"GPU: " + g_gpuName + L"\n\nGPUKeepAlive is currently using this GPU.";
    MessageBoxW(hwnd, info.c_str(), L"Active GPU", MB_OK);
}

static void ShowVersionInfo(HWND hwnd)
{
    const std::wstring info =
        L"GPUKeepAlive\n"
        L"Version " + std::wstring(APP_VERSION) +
        L"\n\nGitHub: github.com/cafali/GPUKeepAlive"
        L"\nMIT License, 2026";
    MessageBoxW(hwnd, info.c_str(), L"Version Info", MB_OK);
}

static void ConfirmHideTrayIcon(HWND hwnd)
{
    if (!IsTrayIconVisible())
        return;

    const int answer = MessageBoxW(
        hwnd,
        L"This will hide the GPUKeepAlive system tray icon until it is re-enabled.\n\nSelect Yes to hide or No to cancel.",
        L"Hide tray icon?",
        MB_YESNO | MB_ICONQUESTION
    );

    if (answer == IDYES)
        HideTrayIcon();
}

static void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    HMENU intervalMenu = CreatePopupMenu();
    if (!menu || !intervalMenu)
    {
        if (menu) DestroyMenu(menu);
        if (intervalMenu) DestroyMenu(intervalMenu);
        return;
    }

    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_15, L"15 sec");
    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_30, L"30 sec");
    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_60, L"1 min");
    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_120, L"2 min (default)");
    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_300, L"5 min");
    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_600, L"10 min");
    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_1800, L"30 min");
    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_3600, L"60 min");
    AppendMenuW(intervalMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_CUSTOM, L"Custom...");

    AppendMenuW(menu, MF_STRING, ID_ACTIVE_GPU, L"Active GPU");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(intervalMenu), L"Set GPU Interval");
    AppendMenuW(menu, MF_STRING, ID_CFG_FOLDER, L"CFG Folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_CHECK_UPDATES, L"Check Updates");
    AppendMenuW(menu, MF_STRING, ID_VISIT_GITHUB, L"Visit on GitHub");
    AppendMenuW(menu, MF_STRING, ID_VERSION_INFO, L"Version Info (1.0.2)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_HIDE_TRAY, L"Hide Tray Icon");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_RESTART, L"Restart");
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

static void RestartApplication()
{
    g_restartRequested.store(true);
    if (g_hwnd)
        DestroyWindow(g_hwnd);
}

static bool FindNvidiaAdapter(ComPtr<IDXGIAdapter1>& nvidiaAdapter, DXGI_ADAPTER_DESC1& selectedDesc)
{
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr))
        return false;

    ComPtr<IDXGIAdapter1> bestAdapter;
    DXGI_ADAPTER_DESC1 bestDesc{};
    UINT64 bestMemory = 0;

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

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        if (desc.VendorId != NVIDIA_VENDOR_ID)
            continue;

        if (desc.DedicatedVideoMemory > bestMemory)
        {
            bestMemory = desc.DedicatedVideoMemory;
            bestDesc = desc;
            bestAdapter = adapter;
        }
    }

    if (!bestAdapter)
        return false;

    nvidiaAdapter = bestAdapter;
    selectedDesc = bestDesc;
    return true;
}

static bool InitGpu()
{
    ComPtr<IDXGIAdapter1> nvidiaAdapter;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    if (!FindNvidiaAdapter(nvidiaAdapter, adapterDesc))
        return false;

    const char* shader =
        "struct V{float4 p:SV_POSITION;};"
        "V VS(uint id:SV_VertexID){"
        "V o;"
        "float2 p[3]={float2(-1,-1),float2(0,1),float2(1,-1)};"
        "o.p=float4(p[id],0,1);"
        "return o;"
        "}"
        "float4 PS():SV_TARGET{return float4(0.02,0.04,0.08,1);}";

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "VS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errors);
    if (FAILED(hr))
        return false;

    errors.Reset();

    hr = D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "PS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errors);
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

    hr = D3D11CreateDevice(nvidiaAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, &got, &context);
    if (hr == E_INVALIDARG)
        hr = D3D11CreateDevice(nvidiaAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels + 1, ARRAYSIZE(levels) - 1, D3D11_SDK_VERSION, &device, &got, &context);

    if (FAILED(hr))
        return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> actualAdapter;
    DXGI_ADAPTER_DESC actualDesc{};

    if (FAILED(device.As(&dxgiDevice)))
        return false;

    if (FAILED(dxgiDevice->GetAdapter(&actualAdapter)))
        return false;

    if (FAILED(actualAdapter->GetDesc(&actualDesc)))
        return false;

    if (actualDesc.VendorId != NVIDIA_VENDOR_ID)
        return false;

    g_gpuName = actualDesc.Description;

    wchar_t idBuffer[256]{};
    swprintf_s(
        idBuffer,
        L"VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%04X&LUID_%08X%08X",
        actualDesc.VendorId,
        actualDesc.DeviceId,
        actualDesc.SubSysId,
        actualDesc.Revision,
        static_cast<unsigned int>(actualDesc.AdapterLuid.HighPart),
        static_cast<unsigned int>(actualDesc.AdapterLuid.LowPart)
    );
    g_gpuIdText = idBuffer;

    UpdateHardwareFile();

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
    if (FAILED(device->CreateTexture2D(&td, nullptr, &texture)))
        return false;

    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &rtv)))
        return false;

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs)))
        return false;

    if (FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps)))
        return false;

    D3D11_VIEWPORT vp{};
    vp.Width = 64.0f;
    vp.Height = 64.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    context->RSSetViewports(1, &vp);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);

    g_renderOk.store(true);
    if (g_trayVisible.load())
        SetStatusTip(L"GPUKeepAlive - NVIDIA active");

    while (g_running.load())
    {
        const float clearColor[4] = { 0.01f, 0.02f, 0.03f, 1.0f };
        context->ClearRenderTargetView(rtv.Get(), clearColor);
        context->Draw(3, 0);
        context->Flush();

        const auto configuredInterval = std::chrono::seconds(g_gpuIntervalSeconds.load());
        const auto start = std::chrono::steady_clock::now();
        const auto deadline = start + configuredInterval;
        while (g_running.load())
        {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0))
                break;

            const auto sleepFor = std::min<std::chrono::steady_clock::duration>(remaining, std::chrono::milliseconds(250));
            std::this_thread::sleep_for(sleepFor);

            if (g_gpuIntervalSeconds.load() != configuredInterval.count())
                break;
        }
    }

    context->ClearState();
    g_renderOk.store(false);
    return true;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == g_taskbarCreatedMsg)
    {
        if (IsTrayIconVisible())
        {
            g_trayRegistered.store(false);
            if (!RestoreTrayIconIfNeeded())
                StartTrayRecoveryWorker(GetModuleHandleW(nullptr));
        }

        return 0;
    }

    if (msg == WM_TRAY)
    {
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU)
            ShowTrayMenu(hwnd);
        else if (lp == WM_LBUTTONDBLCLK)
        {
            if (FileExists(ReadmePath()))
                OpenFileWithDefaultApp(ReadmePath());
        }
        return 0;
    }

    if (msg == WM_COMMAND)
    {
        const UINT id = LOWORD(wp);

        if (id == ID_ACTIVE_GPU)
            ShowActiveGpuMessage(hwnd);
        else if (id == ID_CFG_FOLDER)
            OpenConfigFolder();
        else if (id == ID_CHECK_UPDATES)
            ShellExecuteW(nullptr, L"open", L"https://github.com/cafali/GPUKeepAlive/releases", nullptr, nullptr, SW_SHOWNORMAL);
        else if (id == ID_VISIT_GITHUB)
            ShellExecuteW(nullptr, L"open", L"https://github.com/cafali/GPUKeepAlive", nullptr, nullptr, SW_SHOWNORMAL);
        else if (id == ID_VERSION_INFO)
            ShowVersionInfo(hwnd);
        else if (id == ID_HIDE_TRAY)
            ConfirmHideTrayIcon(hwnd);
        else if (id == ID_RESTART)
            RestartApplication();
        else if (id == ID_INTERVAL_30)
        {
            SaveIntervalSetting(30);
            RestartApplication();
        }
        else if (id == ID_INTERVAL_60)
        {
            SaveIntervalSetting(60);
            RestartApplication();
        }
        else if (id == ID_INTERVAL_120)
        {
            SaveIntervalSetting(120);
            RestartApplication();
        }
        else if (id == ID_INTERVAL_300)
        {
            SaveIntervalSetting(300);
            RestartApplication();
        }
        else if (id == ID_INTERVAL_600)
        {
            SaveIntervalSetting(600);
            RestartApplication();
        }
        else if (id == ID_INTERVAL_15)
        {
            SaveIntervalSetting(15);
            RestartApplication();
        }
        else if (id == ID_INTERVAL_1800)
        {
            SaveIntervalSetting(1800);
            RestartApplication();
        }
        else if (id == ID_INTERVAL_3600)
        {
            SaveIntervalSetting(3600);
            RestartApplication();
        }
        else if (id == ID_INTERVAL_CUSTOM)
            OpenIntervalConfigFile();
        else if (id == ID_EXIT)
            DestroyWindow(hwnd);

        return 0;
    }

    if (msg == WM_DESTROY)
    {
        if (g_trayRegistered.load())
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayRegistered.store(false);
        if (g_nid.hIcon)
        {
            DestroyIcon(g_nid.hIcon);
            g_nid.hIcon = nullptr;
        }

        g_running = false;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    EnsureConfigDirectory();
    EnsureDefaultTrayConfig();
    EnsureDefaultIntervalConfig();

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\GPUKeepAlive_SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
            CloseHandle(mutex);
        return 0;
    }

    g_trayVisible.store(TraySetting());
    g_gpuIntervalSeconds.store(ReadIntervalSetting());

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"GPUKeepAliveHiddenWindow";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"GPUKeepAlive", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!g_hwnd)
    {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 2;
    }

    if (g_trayVisible.load())
    {
        if (!RegisterTrayIcon(instance))
            StartTrayRecoveryWorker(instance);
    }

    std::thread gpu([]()
    {
        if (!InitGpu())
        {
            g_renderOk.store(false);
            if (g_running.load() && g_hwnd)
            {
                MessageBoxW(g_hwnd,
                    L"NVIDIA GPU initialization failed.\n\nNo NVIDIA GPU was selected or Direct3D 11 initialization failed.\n\nCheck your NVIDIA driver.",
                    L"GPUKeepAlive - Error",
                    MB_ICONWARNING | MB_OK);
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
    {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = nullptr;
    }

    const bool shouldRestart = g_restartRequested.load();

    ReleaseMutex(mutex);
    CloseHandle(mutex);

    if (shouldRestart)
        LaunchCurrentExecutable();

    return 0;
}
