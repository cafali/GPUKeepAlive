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
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
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
static constexpr UINT ID_OPEN_INSTALL_FOLDER = 2006;
static constexpr UINT ID_OPEN_SETTINGS_FOLDER = 2007;
static constexpr UINT ID_HIDE_TRAY = 2008;
static constexpr UINT ID_RESTART = 2009;
static constexpr UINT ID_EXIT = 2010;
static constexpr UINT ID_PAUSE = 2011;
static constexpr UINT ID_VERSION_INFO = 2012;
static constexpr UINT ID_EDIT_CONFIG_FILE = 2013;

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
static constexpr wchar_t APP_VERSION[] = L"1.0.4";

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_renderOk{false};
static std::atomic<int> g_gpuIntervalSeconds{DEFAULT_INTERVAL_SECONDS};
static std::atomic<bool> g_trayVisible{true};
static std::atomic<bool> g_paused{false};
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

static std::string TrimString(const std::string& value)
{
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

static bool ReadConfigLines(const std::wstring& path, std::vector<std::string>& lines)
{
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream.is_open())
        return false;
    std::string line;
    while (std::getline(stream, line))
        lines.push_back(line);
    return !stream.bad();
}

static bool ParseInteger(const std::string& value, long& result)
{
    errno = 0;
    char* end = nullptr;
    result = std::strtol(value.c_str(), &end, 10);
    return end != value.c_str() && *end == '\0' && errno != ERANGE;
}

static bool FindSetting(const std::vector<std::string>& lines, const char* key, std::string& value)
{
    for (const auto& line : lines)
    {
        std::string trimmed = TrimString(line);
        if (trimmed.compare(0, 3, "\xEF\xBB\xBF") == 0)
            trimmed = TrimString(trimmed.substr(3));
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
            continue;
        const auto equals = trimmed.find('=');
        if (equals != std::string::npos && _stricmp(TrimString(trimmed.substr(0, equals)).c_str(), key) == 0)
        {
            value = TrimString(trimmed.substr(equals + 1));
            return true;
        }
    }
    return false;
}

static void ConfigError(const wchar_t* message)
{
    MessageBoxW(nullptr, message, L"GPUKeepAlive - Settings Error", MB_OK | MB_ICONERROR);
}

static bool WriteConfigLines(const std::vector<std::string>& lines)
{
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempFileNameW(ConfigDir().c_str(), L"gka", 0, temporary))
        return false;

    bool written = false;
    {
        std::ofstream stream(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
        if (stream.is_open())
        {
            for (const auto& line : lines)
                stream << line << '\n';
            stream.close();
            written = !stream.fail();
        }
    }
    if (written)
        written = MoveFileExW(temporary, ConfigPath(L"set_gpukeepalive.ini").c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!written)
        DeleteFileW(temporary);
    return written;
}

static bool EnsureSettingsConfig()
{
    std::error_code ec;
    std::filesystem::create_directories(ConfigDir(), ec);
    if (ec)
        return false;
    if (FileExists(ConfigPath(L"set_gpukeepalive.ini")))
        return true;

    long tray = 1;
    long interval = DEFAULT_INTERVAL_SECONDS;
    const std::wstring trayPath = ConfigPath(L"tray.ini");
    std::wstring intervalPath = ConfigPath(L"interval.ini");
    if (!FileExists(intervalPath))
        intervalPath = ConfigPath(L"interval.cfg");

    std::vector<std::string> lines;
    std::string value;
    long parsed = 0;
    if (FileExists(trayPath))
    {
        if (!ReadConfigLines(trayPath, lines))
            return false;
        if (FindSetting(lines, "Tray", value) && !value.empty())
        {
            if (!ParseInteger(value, parsed))
                return false;
            tray = parsed != 0 ? 1 : 0;
        }
    }
    if (FileExists(intervalPath))
    {
        lines.clear();
        if (!ReadConfigLines(intervalPath, lines))
            return false;
        bool hasValue = false;
        bool foundValue = false;
        for (const auto& line : lines)
        {
            value = TrimString(line);
            if (value.compare(0, 3, "\xEF\xBB\xBF") == 0)
                value = TrimString(value.substr(3));
            if (value.empty() || value[0] == '#' || value[0] == ';' || value[0] == '[')
                continue;
            const auto equals = value.find('=');
            if (equals != std::string::npos)
                value = TrimString(value.substr(equals + 1));
            if (value.empty())
                continue;
            hasValue = true;
            errno = 0;
            char* end = nullptr;
            parsed = std::strtol(value.c_str(), &end, 10);
            if (end != value.c_str() && errno != ERANGE && parsed > 0 && parsed <= INT_MAX)
            {
                interval = parsed;
                foundValue = true;
                break;
            }
        }
        if (hasValue && !foundValue)
            return false;
    }

    lines = {
        "## GPUKeepAlive Settings - Lightweight GPU activity maintenance utility",
        "",
        "## Enable or disable the system tray icon | 0 = Disabled | 1 = Enabled",
        "",
        "Tray=" + std::to_string(tray),
        "",
        "## Rendering interval in seconds | Default value is 120 (120s/2min)",
        "",
        "Interval=" + std::to_string(interval)
    };
    if (!WriteConfigLines(lines))
        return false;

    for (const auto& legacyPath : {trayPath, intervalPath})
    {
        ec.clear();
        std::filesystem::remove(legacyPath, ec);
        if (ec)
            ConfigError(L"Settings were migrated, but an old configuration file could not be removed.");
    }
    return true;
}

static bool LoadSettings()
{
    std::vector<std::string> lines;
    if (!ReadConfigLines(ConfigPath(L"set_gpukeepalive.ini"), lines))
    {
        ConfigError(L"Could not read set_gpukeepalive.ini.");
        return false;
    }

    std::string value;
    long parsed = 0;
    if (FindSetting(lines, "Tray", value) && !value.empty())
    {
        if (!ParseInteger(value, parsed) || (parsed != 0 && parsed != 1))
        {
            ConfigError(L"Tray must be 0 or 1 in set_gpukeepalive.ini.");
            return false;
        }
        g_trayVisible.store(parsed == 1);
    }
    if (FindSetting(lines, "Interval", value) && !value.empty())
    {
        if (!ParseInteger(value, parsed) || parsed <= 0 || parsed > INT_MAX)
        {
            ConfigError(L"Interval must be a positive number of seconds in set_gpukeepalive.ini.");
            return false;
        }
        g_gpuIntervalSeconds.store(static_cast<int>(parsed));
    }
    return true;
}

static bool SaveSetting(const char* key, int value)
{
    std::vector<std::string> lines;
    if (!ReadConfigLines(ConfigPath(L"set_gpukeepalive.ini"), lines))
    {
        ConfigError(L"Could not read set_gpukeepalive.ini. The setting was not changed.");
        return false;
    }
    bool found = false;
    for (auto& line : lines)
    {
        std::string trimmed = TrimString(line);
        const bool hasBom = trimmed.compare(0, 3, "\xEF\xBB\xBF") == 0;
        if (hasBom)
            trimmed = TrimString(trimmed.substr(3));
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
            continue;
        const auto equals = trimmed.find('=');
        if (equals != std::string::npos && _stricmp(TrimString(trimmed.substr(0, equals)).c_str(), key) == 0)
        {
            const bool hadCarriageReturn = !line.empty() && line.back() == '\r';
            line = (hasBom ? "\xEF\xBB\xBF" : "") + std::string(key) + "=" +
                std::to_string(value) + (hadCarriageReturn ? "\r" : "");
            found = true;
            break;
        }
    }
    if (!found)
        lines.push_back(std::string(key) + "=" + std::to_string(value));
    if (!WriteConfigLines(lines))
    {
        ConfigError(L"Could not write set_gpukeepalive.ini. The setting was not changed.");
        return false;
    }
    return true;
}

static void UpdateHardwareFile()
{
    EnsureConfigDirectory();
    const std::wstring gpuDataLog = ConfigPath(L"gpudata.log");
    const std::wstring legacyPath = ConfigPath(L"hardware.txt");
    if (!FileExists(gpuDataLog) && FileExists(legacyPath))
    {
        std::error_code ec;
        std::filesystem::rename(legacyPath, gpuDataLog, ec);
    }

    std::wstring hardwareText = L"GPU: " + g_gpuName + L"\n";
    hardwareText += L"GPU Identifier: " + g_gpuIdText + L"\n";
    hardwareText += L"GPUKeepAlive active\n";

    WriteTextFile(gpuDataLog, hardwareText);

    std::error_code ec;
    std::filesystem::remove(ConfigPath(L"GPUid.txt"), ec);
}

static void OpenInstallationFolder()
{
    std::wstring installPath = AppDir();
    ShellExecuteW(nullptr, L"open", installPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void OpenSettingsFolder()
{
    EnsureConfigDirectory();
    ShellExecuteW(nullptr, L"open", ConfigDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void OpenFileWithDefaultApp(const std::wstring& path)
{
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        ConfigError(L"Could not open set_gpukeepalive.ini with its default application.");
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

static HICON LoadTrayIcon(HINSTANCE instance, bool paused)
{
    return reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(paused ? IDI_PAUSE : IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
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
    g_nid.hIcon = LoadTrayIcon(instance, g_paused.load());

    if (!g_nid.hIcon)
    {
        g_trayRegistered.store(false);
        return false;
    }

    wcscpy_s(g_nid.szTip, g_paused.load() ? L"GPUKeepAlive - Paused" : L"GPUKeepAlive - Active");
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

static void TogglePause()
{
    const bool paused = !g_paused.load();
    g_paused.store(paused);

    if (!g_trayRegistered.load())
        return;

    HICON icon = LoadTrayIcon(GetModuleHandleW(nullptr), paused);
    if (!icon)
        return;

    HICON oldIcon = g_nid.hIcon;
    g_nid.hIcon = icon;
    wcsncpy_s(g_nid.szTip, paused ? L"GPUKeepAlive - Paused" : L"GPUKeepAlive - Active", _TRUNCATE);
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);

    if (oldIcon)
        DestroyIcon(oldIcon);
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

    if (SaveSetting("Tray", 0))
        RestartApplication();
}

static void OpenSettingsConfigFile()
{
    OpenFileWithDefaultApp(ConfigPath(L"set_gpukeepalive.ini"));
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
    HMENU appMenu = CreatePopupMenu();
    if (!menu || !intervalMenu || !appMenu)
    {
        if (menu) DestroyMenu(menu);
        if (intervalMenu) DestroyMenu(intervalMenu);
        if (appMenu) DestroyMenu(appMenu);
        MessageBoxW(hwnd, L"Could not create the tray menu.", L"GPUKeepAlive - Error", MB_OK | MB_ICONERROR);
        return;
    }

    const int interval = g_gpuIntervalSeconds.load();
    AppendMenuW(intervalMenu, MF_STRING | (interval == 15 ? MF_CHECKED : MF_UNCHECKED), ID_INTERVAL_15, L"15 sec");
    AppendMenuW(intervalMenu, MF_STRING | (interval == 30 ? MF_CHECKED : MF_UNCHECKED), ID_INTERVAL_30, L"30 sec");
    AppendMenuW(intervalMenu, MF_STRING | (interval == 60 ? MF_CHECKED : MF_UNCHECKED), ID_INTERVAL_60, L"1 min");
    AppendMenuW(intervalMenu, MF_STRING | (interval == 120 ? MF_CHECKED : MF_UNCHECKED), ID_INTERVAL_120, L"2 min (default)");
    AppendMenuW(intervalMenu, MF_STRING | (interval == 300 ? MF_CHECKED : MF_UNCHECKED), ID_INTERVAL_300, L"5 min");
    AppendMenuW(intervalMenu, MF_STRING | (interval == 600 ? MF_CHECKED : MF_UNCHECKED), ID_INTERVAL_600, L"10 min");
    AppendMenuW(intervalMenu, MF_STRING | (interval == 1800 ? MF_CHECKED : MF_UNCHECKED), ID_INTERVAL_1800, L"30 min");
    AppendMenuW(intervalMenu, MF_STRING | (interval == 3600 ? MF_CHECKED : MF_UNCHECKED), ID_INTERVAL_3600, L"60 min");
    AppendMenuW(intervalMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(intervalMenu, MF_STRING, ID_INTERVAL_CUSTOM, L"Custom...");

    AppendMenuW(menu, MF_STRING, ID_ACTIVE_GPU, L"Active GPU");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(intervalMenu), L"Set GPU Interval");
    
    HMENU goToMenu = CreatePopupMenu();
    if (goToMenu)
    {
        AppendMenuW(goToMenu, MF_STRING, ID_OPEN_INSTALL_FOLDER, L"Installation Folder");
        AppendMenuW(goToMenu, MF_STRING, ID_OPEN_SETTINGS_FOLDER, L"Settings Folder");
        AppendMenuW(goToMenu, MF_STRING, ID_EDIT_CONFIG_FILE, L"Edit Config File");
        AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(goToMenu), L"Go to");
    }
    
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(appMenu, MF_STRING, ID_CHECK_UPDATES, L"Check Updates");
    AppendMenuW(appMenu, MF_STRING, ID_VISIT_GITHUB, L"Visit on GitHub");
    const std::wstring versionLabel = L"Version Info (" + std::wstring(APP_VERSION) + L")";
    AppendMenuW(appMenu, MF_STRING, ID_VERSION_INFO, versionLabel.c_str());
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(appMenu), L"GPUKeepAlive");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_HIDE_TRAY, L"Hide Tray Icon");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_PAUSE, g_paused.load() ? L"Resume" : L"Pause");
    AppendMenuW(menu, MF_STRING, ID_RESTART, L"Restart");
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
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
    if (g_trayVisible.load() && !g_paused.load())
        SetStatusTip(L"GPUKeepAlive - Active");

    while (g_running.load())
    {
        if (g_paused.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

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

            if (g_gpuIntervalSeconds.load() != configuredInterval.count() || g_paused.load())
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
            TogglePause();
        return 0;
    }

    if (msg == WM_COMMAND)
    {
        const UINT id = LOWORD(wp);

        if (id == ID_ACTIVE_GPU)
            ShowActiveGpuMessage(hwnd);
        else if (id == ID_OPEN_INSTALL_FOLDER)
            OpenInstallationFolder();
        else if (id == ID_OPEN_SETTINGS_FOLDER)
            OpenSettingsFolder();
        else if (id == ID_CHECK_UPDATES)
            ShellExecuteW(nullptr, L"open", L"https://github.com/cafali/GPUKeepAlive/releases", nullptr, nullptr, SW_SHOWNORMAL);
        else if (id == ID_VISIT_GITHUB)
            ShellExecuteW(nullptr, L"open", L"https://github.com/cafali/GPUKeepAlive", nullptr, nullptr, SW_SHOWNORMAL);
        else if (id == ID_VERSION_INFO)
            ShowVersionInfo(hwnd);
        else if (id == ID_HIDE_TRAY)
            ConfirmHideTrayIcon(hwnd);
        else if (id == ID_PAUSE)
            TogglePause();
        else if (id == ID_RESTART)
            RestartApplication();
        else if (id == ID_INTERVAL_30)
        {
            if (SaveSetting("Interval", 30))
                RestartApplication();
        }
        else if (id == ID_INTERVAL_60)
        {
            if (SaveSetting("Interval", 60))
                RestartApplication();
        }
        else if (id == ID_INTERVAL_120)
        {
            if (SaveSetting("Interval", 120))
                RestartApplication();
        }
        else if (id == ID_INTERVAL_300)
        {
            if (SaveSetting("Interval", 300))
                RestartApplication();
        }
        else if (id == ID_INTERVAL_600)
        {
            if (SaveSetting("Interval", 600))
                RestartApplication();
        }
        else if (id == ID_INTERVAL_15)
        {
            if (SaveSetting("Interval", 15))
                RestartApplication();
        }
        else if (id == ID_INTERVAL_1800)
        {
            if (SaveSetting("Interval", 1800))
                RestartApplication();
        }
        else if (id == ID_INTERVAL_3600)
        {
            if (SaveSetting("Interval", 3600))
                RestartApplication();
        }
        else if (id == ID_INTERVAL_CUSTOM || id == ID_EDIT_CONFIG_FILE)
            OpenSettingsConfigFile();
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

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\GPUKeepAlive_SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
            CloseHandle(mutex);
        return 0;
    }

    if (!EnsureSettingsConfig())
    {
        ConfigError(L"Could not create set_gpukeepalive.ini or migrate the existing settings. Check the settings files and folder permissions.");
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }
    if (!LoadSettings())
    {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }

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
