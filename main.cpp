#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <richedit.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <tuple>
#include <cwchar>
#include <memory>
#include <ctime>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "ws2_32.lib")

#include <wrl/client.h>
#include <wrl/event.h>
#include "WebView2.h"

#include "replay_buffer.h"
#include "upload_window.h"

// ── Build-time config ──────────────────────────────────────────────────────────
static constexpr wchar_t CLIENT_DOWNLOAD_URL[] =
    L"https://client.wow-hc.com/1.14.2/WOW-1.14.2.zip";
   // L"https://dl.wow-hc.com/clients/WOW-Classic-1.14.2.zip";

static constexpr wchar_t CLIENT_112_DOWNLOAD_URL[] =
    L"https://client.wow-hc.com/1.12.1/WOW-1.12.1.zip";

static constexpr wchar_t REALM_NORMAL_SERVER[] = L"logon-eu-0.wow-hc.com";
static constexpr wchar_t REALM_PTR_SERVER[]    = L"ptr-logon-eu-2.wow-hc.com";

static constexpr wchar_t APP_NAME[]        = L"WOW-HC Launcher";
static constexpr wchar_t HERMES_GH_OWNER[] = L"Novivy";
static constexpr wchar_t HERMES_GH_REPO[]  = L"HermesProxy";
static constexpr wchar_t ADDON_GH_OWNER[]      = L"Novivy";
static constexpr wchar_t ADDON_GH_REPO[]       = L"wow-hc-addon";
static constexpr wchar_t LAUNCHER_GH_OWNER[]   = L"Novivy";
static constexpr wchar_t LAUNCHER_GH_REPO[]    = L"wowhc-launcher";
static constexpr char    LAUNCHER_EXE_ASSET[]      = "WOW-HC-Launcher.exe";
static constexpr char    LAUNCHER_FULL_ZIP_ASSET[] = "WOW-HC-Launcher-Full.zip";

#ifndef LAUNCHER_VERSION_STR
#define LAUNCHER_VERSION_STR "v0.0.0-dev"
#endif

// ── Resource IDs ───────────────────────────────────────────────────────────────
static const int IDR_LOGO_CLEAN = 201;
static const int IDR_LOGO_ROUND = 202;

// ── Dark-mode palette ──────────────────────────────────────────────────────────
static const COLORREF CLR_BG     = RGB(10,   8,   6);
static const COLORREF CLR_BG2    = RGB(29,  24,  16);
static const COLORREF CLR_TEXT   = RGB(236, 218, 176);
static const COLORREF CLR_DIM    = RGB(106,  86,  56);
static const COLORREF CLR_ACCENT = RGB(224, 160,  74);
static const COLORREF CLR_BAR    = RGB(224, 160,  74);
static const COLORREF CLR_SEP    = RGB(46,  34,  18);

// ── Control / message IDs ──────────────────────────────────────────────────────
enum : UINT {
    ID_BTN_PLAY      = 101,
    ID_BTN_BROWSE    = 102,
    ID_EDIT_PATH     = 103,
    ID_PROGRESS      = 104,
    ID_STATIC_STATUS = 105,
    ID_BTN_TRANSFER  = 106,
    ID_LINK_WEBSITE  = 107,
    ID_BTN_OPEN      = 108,
    ID_LINK_ADDONS   = 109,
    ID_BTN_RECORD        = 110,
    ID_STATIC_RB_STATUS  = 111,
    ID_BTN_SAVE_REPLAY      = 112,
    ID_BTN_UPLOAD           = 113,
    ID_BTN_RECORD_SETTINGS  = 114,
    ID_EDIT_CONSOLE         = 115,
    ID_COMBO_REALM          = 116,
    ID_TIMER_UPDATE         = 200,
    ID_TIMER_HOVER          = 201,
    ID_TIMER_LIVE           = 202,
};

#define WM_WORKER_STATUS    (WM_APP + 1)  // wParam = WorkerStatus
#define WM_WORKER_PROGRESS  (WM_APP + 2)  // wParam = 0-100
#define WM_WORKER_DONE      (WM_APP + 3)  // wParam = 1 success / 0 fail
#define WM_WORKER_TEXT      (WM_APP + 4)  // lParam = new std::wstring*
#define WM_SET_INSTALL_MODE (WM_APP + 5)  // wParam = 0 not installed / 1 installed
#define WM_TRANSFER_DONE    (WM_APP + 6)  // wParam = 1 success / 0 fail
#define WM_ASK_UPDATE       (WM_APP + 7)  // wParam = UpdateComponent, lParam = new wstring* "remote\nlocal"
#define WM_APPLY_SELF_UPD   (WM_APP + 8)  // lParam = new wstring* (new exe path)
#define WM_STARTUP_CHECK_DONE (WM_APP + 9) // startup launcher-update check finished
#define WM_HERMES_LINE        (WM_APP + 10) // lParam = new std::wstring* (one output line)
#define WM_HERMES_CLOSED      (WM_APP + 11) // HermesProxy terminated — collapse console
#define WM_LIVE_DATA_JSON     (WM_APP + 12) // lParam = new std::wstring* (JSON to post to WebView)
#define WM_SET_REALM          (WM_APP + 13) // wParam = realm index — deferred from WebView2 callback
#define WM_REC_SETTINGS_OPEN   (WM_APP + 31) // open record-settings React modal
#define WM_REC_SETTINGS_BROWSE (WM_APP + 32) // open save-folder picker, result posted back to WebView
#define WM_REC_SETTINGS_TOGGLE (WM_APP + 33) // lParam = new std::string* (JSON), toggle recording
#define WM_REC_SETTINGS_CLOSE  (WM_APP + 34) // lParam = new std::string* (JSON), save & close modal
#define WM_GEN_SETTINGS_CLOSE  (WM_APP + 35) // lParam = new std::string* (JSON), save & close general settings

enum UpdateComponent : WPARAM { UC_HERMES = 0, UC_ADDON = 1, UC_LAUNCHER = 2 };

enum ClientType : int { CT_UNKNOWN = 0, CT_114 = 1, CT_112 = 2 };

enum WorkerStatus : int {
    WS_CHECKING = 0,
    WS_DL_HERMES,
    WS_EX_HERMES,
    WS_DL_CLIENT,
    WS_EX_CLIENT,
    WS_DL_ADDON,
    WS_EX_ADDON,
    WS_TRANSFER,
    WS_READY,
    WS_ERROR,
    WS_LAUNCHING,
    WS_NO_PATH,
};

static const wchar_t* STATUS_TEXT[] = {
    L"Checking for updates...",
    L"Downloading HermesProxy update...",
    L"Extracting HermesProxy...",
    L"Downloading client...",
    L"Installing client (it may take a few minutes)...",
    L"Downloading WOW_HC addon...",
    L"Installing WOW_HC addon...",
    L"Transferring UI, macros, addons and settings...",
    L"Ready to Play",
    L"Error - check your connection or installation path.",
    L"Launching...",
    L"Select a new or existing installation folder",
};

// ── Globals ────────────────────────────────────────────────────────────────────
static HINSTANCE g_hInst = nullptr;
static HWND      g_hwnd  = nullptr;

// WebView2
static Microsoft::WRL::ComPtr<ICoreWebView2>           g_webview;
static Microsoft::WRL::ComPtr<ICoreWebView2Controller> g_wvCtrl;
static bool g_wvReady    = false;
static bool g_wvPageReady = false; // set when JS sends "ready" (page fully loaded)

// React modal handshake (nested message-pump pattern, same as native modal loops)
static bool g_reactModalDone   = false;
static int  g_reactModalResult = 0;    // 0=cancel/back, 1=first option, 2=second option

// App state mirrored to WebView
static std::wstring g_currentStatus;
static int          g_currentProgress   = 0;
static bool         g_showConsole       = false;
static int          g_consoleLineCount  = 0;

// ANSI 16-color palette (used by HermesProxy console rendering)
static const COLORREF g_ansiColors[16] = {
    RGB(0,0,0),       RGB(128,0,0),     RGB(0,128,0),     RGB(128,128,0),
    RGB(0,0,128),     RGB(128,0,128),   RGB(0,128,128),   RGB(192,192,192),
    RGB(128,128,128), RGB(255,0,0),     RGB(0,255,0),     RGB(255,255,0),
    RGB(0,0,255),     RGB(255,0,255),   RGB(0,255,255),   RGB(255,255,255),
};

// Taskbar
static ITaskbarList3* g_pTaskbar          = nullptr;
static ULONG_PTR      g_gdiplusToken      = 0;
static UINT           g_taskbarCreatedMsg = 0;
static HICON          g_hIconLarge        = nullptr;
static HICON          g_hIconSmall        = nullptr;
static HBRUSH         g_hbrBg             = nullptr; // still used by modal dialogs
static HBRUSH         g_hbrBg2            = nullptr;
static HICON          g_hIconRecordingOverlay = nullptr;

// Taskbar progress cache
static int  g_taskbarLastPct     = 0;
static bool g_taskbarHasProgress = false;

static UINT g_dpi = 96;

// Install / client paths
static std::wstring g_installPath;
static std::wstring g_clientPath;
static std::wstring g_hermesExePath;
static std::wstring g_arctiumExePath;
static std::wstring g_wowTweakedExePath;
static std::wstring g_configDir;
static std::wstring g_logPath;

static std::atomic<bool> g_workerBusy{false};
static std::atomic<bool> g_playReady{false};
static std::atomic<bool> g_clientInstalled{false};
static std::atomic<bool> g_isLaunching{false};
static std::atomic<bool> g_ffmpegBusy{false};

static bool       g_testMode              = false; // --test flag: pulls latest pre-release
static bool       g_devMode               = false; // --dev flag: forces dev-mode behaviour (DevTools, skip update fetches)
static bool       g_freshInstall          = false;
static ClientType g_clientType            = CT_UNKNOWN;
static ClientType g_pendingInstallType    = CT_UNKNOWN;
static bool       g_pendingExistingInstall = false;
static int        g_realmIndex            = 0;
static bool       g_showRecordingNotifications = true;

// HermesProxy pipe
static HANDLE g_hermesProcess  = nullptr;
static HANDLE g_hermesPipeRead = nullptr;
static HMODULE g_hRichEdit     = nullptr; // still loaded for existing RichEdit code paths

static constexpr int WND_CLIENT_W = 875;
static constexpr int WND_CLIENT_H = 530;

// ── Shared fonts for modal dialogs (lazily created, never deleted — process lifetime) ──
static HFONT DlgFont()
{
    static HFONT s = nullptr;
    if (!s) {
        LOGFONTW lf = {};
        lf.lfHeight  = -MulDiv(9, g_dpi, 72);
        lf.lfWeight  = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        s = CreateFontIndirectW(&lf);
    }
    return s;
}
static HFONT DlgSmallFont()
{
    static HFONT s = nullptr;
    if (!s) {
        LOGFONTW lf = {};
        lf.lfHeight  = -MulDiv(8, g_dpi, 72);
        lf.lfWeight  = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        s = CreateFontIndirectW(&lf);
    }
    return s;
}

// ── Config helpers ─────────────────────────────────────────────────────────────
static std::wstring ConfigPath()     { return g_configDir + L"\\launcher.ini"; }

static void AppendLog(const wchar_t* fmt, ...)
{
    if (g_logPath.empty()) return;
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t header[32], body[1024];
    swprintf_s(header, L"[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vswprintf_s(body, fmt, a); va_end(a);
    std::wstring line = header + std::wstring(body) + L"\r\n";
    int sz = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (sz <= 1) return;
    std::string u8(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, u8.data(), sz, nullptr, nullptr);
    HANDLE hf = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD written; WriteFile(hf, u8.c_str(), (DWORD)u8.size(), &written, nullptr);
        CloseHandle(hf);
    }
}
static std::wstring ClientMarker()   { return g_clientPath + L"\\.launcher_installed"; }
static std::wstring GetAddonsDir()
{
    if (g_clientType == CT_112) {
        // Launcher-downloaded ZIPs always have _classic_era_.
        // Existing flat installs (Wow_tweaked.exe at clientPath root) don't.
        std::wstring era = g_clientPath + L"\\_classic_era_";
        if (GetFileAttributesW(era.c_str()) != INVALID_FILE_ATTRIBUTES)
            return era + L"\\Interface\\AddOns";
        return g_clientPath + L"\\Interface\\AddOns";
    }
    return g_clientPath + L"\\_classic_era_\\Interface\\AddOns";
}

static void SaveConfig()
{
    std::wstring iniPath = ConfigPath();
    const wchar_t* ini = iniPath.c_str();
    WritePrivateProfileStringW(L"Launcher", L"InstallPath",  g_installPath.c_str(),    ini);
    WritePrivateProfileStringW(L"Launcher", L"ClientPath",   g_clientPath.c_str(),     ini);
    WritePrivateProfileStringW(L"Launcher", L"HermesExePath", g_hermesExePath.c_str(), ini);
    WritePrivateProfileStringW(L"Launcher", L"ArticulumExePath", g_arctiumExePath.c_str(), ini);
    wchar_t ctBuf[8]; swprintf_s(ctBuf, L"%d", (int)g_clientType);
    WritePrivateProfileStringW(L"Launcher", L"ClientType", ctBuf, ini);
    WritePrivateProfileStringW(L"Launcher", L"WowTweakedExePath", g_wowTweakedExePath.c_str(), ini);
    wchar_t riBuf[8]; swprintf_s(riBuf, L"%d", g_realmIndex);
    WritePrivateProfileStringW(L"Launcher", L"RealmIndex", riBuf, ini);
    WritePrivateProfileStringW(L"Launcher", L"ShowRecordingNotifications", g_showRecordingNotifications ? L"1" : L"0", ini);
}

static void LoadConfig()
{
    wchar_t buf[MAX_PATH] = {};
    std::wstring iniPath = ConfigPath();
    const wchar_t* ini = iniPath.c_str();
    auto Rd = [&](const wchar_t* key) -> std::wstring {
        memset(buf, 0, sizeof(buf));
        GetPrivateProfileStringW(L"Launcher", key, L"", buf, MAX_PATH, ini);
        return buf;
    };
    g_installPath    = Rd(L"InstallPath");
    g_clientPath     = Rd(L"ClientPath");
    g_hermesExePath  = Rd(L"HermesExePath");
    g_arctiumExePath = Rd(L"ArticulumExePath");
    g_wowTweakedExePath = Rd(L"WowTweakedExePath");
    {
        wchar_t ctBuf[8] = {};
        GetPrivateProfileStringW(L"Launcher", L"ClientType", L"0", ctBuf, 8, ini);
        int ct = _wtoi(ctBuf);
        g_clientType = (ct == 1) ? CT_114 : (ct == 2) ? CT_112 : CT_UNKNOWN;
    }
    // Backward compat: derive ClientPath from old InstallPath-only configs
    if (g_clientPath.empty() && !g_installPath.empty())
        g_clientPath = g_installPath + L"\\client";
    // Backward compat: derive InstallPath from configs that only saved ClientPath
    if (g_installPath.empty() && !g_clientPath.empty())
        g_installPath = g_clientPath;
    // Backward compat: assume CT_114 for old configs that have HermesExePath
    if (g_clientType == CT_UNKNOWN && !g_hermesExePath.empty())
        g_clientType = CT_114;
    {
        wchar_t ri[8] = {};
        GetPrivateProfileStringW(L"Launcher", L"RealmIndex", L"0", ri, 8, ini);
        int idx = _wtoi(ri);
        g_realmIndex = (idx == 1) ? 1 : 0;
    }
    {
        wchar_t rn[8] = {};
        GetPrivateProfileStringW(L"Launcher", L"ShowRecordingNotifications", L"1", rn, 8, ini);
        g_showRecordingNotifications = (_wtoi(rn) != 0);
        RB_SetShowNotifications(g_showRecordingNotifications);
    }
}

// ── EXE version reader ────────────────────────────────────────────────────────
static bool GetExeVersion(const std::wstring& path, int& major, int& minor, int& patch)
{
    DWORD dummy;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (!size) return false;
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buf.data())) return false;
    VS_FIXEDFILEINFO* pInfo = nullptr; UINT infoSize = 0;
    if (!VerQueryValueW(buf.data(), L"\\", (void**)&pInfo, &infoSize) || !pInfo) return false;
    major = HIWORD(pInfo->dwFileVersionMS);
    minor = LOWORD(pInfo->dwFileVersionMS);
    patch = HIWORD(pInfo->dwFileVersionLS);
    return true;
}

static std::wstring GetLocalHermesVersion()
{
    auto fromExe = [](const std::wstring& path) -> std::wstring {
        if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return {};
        int maj = 0, min = 0, pat = 0;
        if (!GetExeVersion(path, maj, min, pat) || maj == 0) return {};
        wchar_t ver[32]; swprintf_s(ver, L"v%d.%d.%d", maj, min, pat);
        return ver;
    };
    std::wstring v = fromExe(g_hermesExePath);
    if (v.empty() && !g_clientPath.empty())
        v = fromExe(g_clientPath + L"\\HermesProxy.exe");
    return v;
}

static std::wstring ReadAddonTocVersion()
{
    if (g_clientPath.empty()) return {};
    std::wstring tocPath = GetAddonsDir() + L"\\WOW_HC\\WOW_HC.toc";
    std::wifstream f(tocPath);
    if (!f.is_open()) return {};
    std::wstring line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' '))
            line.pop_back();
        if (line.rfind(L"## Version:", 0) == 0) {
            std::wstring ver = line.substr(11);
            size_t start = ver.find_first_not_of(L" \t");
            return start != std::wstring::npos ? ver.substr(start) : L"";
        }
    }
    return {};
}

static void WriteLastCheckTime()
{
    wchar_t buf[32];
    swprintf_s(buf, L"%I64d", (long long)time(nullptr));
    WritePrivateProfileStringW(L"Launcher", L"LastCheckTime", buf, ConfigPath().c_str());
}

static void ResetLastCheckTime()
{
    WritePrivateProfileStringW(L"Launcher", L"LastCheckTime", L"0", ConfigPath().c_str());
}

// ── Version comparison ─────────────────────────────────────────────────────────
static std::tuple<int,int,int> ParseVersion(const std::wstring& s)
{
    std::wstring v = s;
    if (!v.empty() && (v[0] == L'v' || v[0] == L'V')) v = v.substr(1);
    int a = 0, b = 0, c = 0;
    swscanf_s(v.c_str(), L"%d.%d.%d", &a, &b, &c);
    return {a, b, c};
}

static bool IsNewer(const std::wstring& local, const std::wstring& remote)
{
    if (local.empty()) return true;
    auto [la,lb,lc] = ParseVersion(local);
    auto [ra,rb,rc] = ParseVersion(remote);
    if (ra != la) return ra > la;
    if (rb != lb) return rb > lb;
    return rc > lc;
}

// ── Tiny JSON field extractor ──────────────────────────────────────────────────
static int JsonInt(const std::string& json, const std::string& key, int def = 0)
{
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    while (++pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {}
    if (pos >= json.size()) return def;
    size_t start = pos;
    if (json[start] == '-') pos++;
    while (pos < json.size() && isdigit((unsigned char)json[pos])) pos++;
    if (pos == start) return def;
    try { return std::stoi(json.substr(start, pos - start)); } catch (...) { return def; }
}

static std::string JsonString(const std::string& json, const std::string& key)
{
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    size_t start = pos + 1, end = start;
    while (end < json.size()) {
        if (json[end] == '"' && json[end-1] != '\\') break;
        ++end;
    }
    if (end >= json.size()) return {};
    return json.substr(start, end - start);
}

static bool JsonBool(const std::string& json, const std::string& key, bool def = false)
{
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    while (++pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {}
    if (pos >= json.size()) return def;
    if (json.compare(pos, 4, "true")  == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return def;
}

static std::wstring JsonStringW(const std::string& json, const std::string& key)
{
    std::string raw = JsonString(json, key);
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            ++i;
            switch (raw[i]) {
                case '"': case '/': case '\\': out += raw[i]; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default:  out += '\\'; out += raw[i]; break;
            }
        } else {
            out += raw[i];
        }
    }
    if (out.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, nullptr, 0);
    std::wstring w(n > 1 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, w.data(), n);
    return w;
}

static std::string FindAssetUrl(const std::string& json)
{
    std::string fallback;
    size_t pos = 0;
    while (true) {
        size_t found = json.find("\"browser_download_url\"", pos);
        if (found == std::string::npos) break;
        std::string url = JsonString(json.substr(found), "browser_download_url");
        if (!url.empty()) {
            std::string lower = url;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("win") != std::string::npos) return url;
            if (lower.rfind(".zip") != std::string::npos && fallback.empty())
                fallback = url;
        }
        pos = found + 1;
    }
    return fallback;
}

static void PostText(std::wstring text); // forward declaration

// ── WinHTTP helpers ────────────────────────────────────────────────────────────
static HINTERNET OpenSession()
{
    return WinHttpOpen(L"WOWHCLauncher/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
}

static bool CrackUrl(const std::wstring& url,
    std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& ssl)
{
    wchar_t hbuf[512] = {}, pbuf[4096] = {};
    URL_COMPONENTS uc = {};
    uc.dwStructSize    = sizeof(uc);
    uc.lpszHostName    = hbuf; uc.dwHostNameLength = 512;
    uc.lpszUrlPath     = pbuf; uc.dwUrlPathLength  = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    host = hbuf; path = pbuf; port = uc.nPort;
    ssl  = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    return true;
}

static HINTERNET MakeRequest(HINTERNET hSess, const std::wstring& url, HINTERNET& hConn)
{
    std::wstring host, path;
    INTERNET_PORT port; bool ssl;
    if (!CrackUrl(url, host, path, port, ssl)) return nullptr;
    hConn = WinHttpConnect(hSess, host.c_str(), port, 0);
    if (!hConn) return nullptr;
    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (ssl) flags |= WINHTTP_FLAG_SECURE;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); hConn = nullptr; return nullptr; }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    return hReq;
}

static std::string HttpGet(const std::wstring& url)
{
    HINTERNET hSess = OpenSession();
    if (!hSess) {
        PostText(L"Update check failed: could not open network session.");
        return {};
    }
    HINTERNET hConn = nullptr;
    HINTERNET hReq  = MakeRequest(hSess, url, hConn);
    if (!hReq) {
        WinHttpCloseHandle(hSess);
        PostText(L"Update check failed: could not connect to server.");
        return {};
    }
    WinHttpAddRequestHeaders(hReq,
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n"
        L"Cache-Control: no-cache\r\nPragma: no-cache",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    std::string body;
    bool sent  = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    bool recvd = sent && WinHttpReceiveResponse(hReq, nullptr);
    if (!sent || !recvd) {
        DWORD err = GetLastError();
        wchar_t msg[128];
        swprintf_s(msg, L"Update check failed: network error (0x%08lX).", err);
        PostText(msg);
    } else {
        DWORD statusCode = 0, scLen = sizeof(statusCode);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &statusCode, &scLen, nullptr);
        if (statusCode == 200) {
            DWORD avail = 0, read = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                std::vector<char> buf(avail + 1);
                WinHttpReadData(hReq, buf.data(), avail, &read);
                body.append(buf.data(), read);
            }
        } else {
            wchar_t msg[256];
            if (statusCode == 403 || statusCode == 429) {
                // Rate-limited: read the reset timestamp from the response header
                wchar_t resetBuf[32] = {};
                DWORD resetLen = sizeof(resetBuf);
                std::wstring resetInfo;
                if (WinHttpQueryHeaders(hReq,
                        WINHTTP_QUERY_CUSTOM,
                        L"X-RateLimit-Reset", resetBuf, &resetLen, nullptr) &&
                    resetBuf[0] != L'\0') {
                    long long ts = _wtoi64(resetBuf);
                    if (ts > 0) {
                        time_t t = (time_t)ts;
                        struct tm tm {};
                        gmtime_s(&tm, &t);
                        wchar_t timeBuf[32];
                        wcsftime(timeBuf, 32, L"%H:%M UTC", &tm);
                        resetInfo = std::wstring(L"\nRate limit resets at ") + timeBuf;
                    }
                }
                swprintf_s(msg, L"GitHub API rate limit reached (HTTP %lu).%ls\n\nThe update check will be skipped for now.",
                    statusCode, resetInfo.c_str());
                MessageBoxW(g_hwnd, msg, L"Update Check", MB_OK | MB_ICONINFORMATION);
            } else if (statusCode != 404) {
                // 404 = "not found" is a valid/expected response (e.g. dev-build tag).
                // Only surface unexpected error codes.
                swprintf_s(msg, L"Update check failed: GitHub API returned HTTP %lu.", statusCode);
                PostText(msg);
            }
        }
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return body;
}

static std::string HttpGetSimple(const std::wstring& url)
{
    HINTERNET hSess = OpenSession();
    if (!hSess) return {};
    HINTERNET hConn = nullptr;
    HINTERNET hReq  = MakeRequest(hSess, url, hConn);
    if (!hReq) { WinHttpCloseHandle(hSess); return {}; }
    std::string body;
    bool sent  = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
    bool recvd = sent && WinHttpReceiveResponse(hReq, nullptr) != FALSE;
    if (sent && recvd) {
        DWORD statusCode = 0, scLen = sizeof(statusCode);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &statusCode, &scLen, nullptr);
        if (statusCode == 200) {
            DWORD avail = 0, read = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                std::vector<char> buf(avail + 1);
                WinHttpReadData(hReq, buf.data(), avail, &read);
                body.append(buf.data(), read);
            }
        }
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return body;
}

static void PostLiveDataMsg(const wchar_t* type, const std::string& rawJsonUtf8)
{
    if (rawJsonUtf8.empty() || !g_hwnd) return;
    int len = MultiByteToWideChar(CP_UTF8, 0, rawJsonUtf8.c_str(), (int)rawJsonUtf8.size(), nullptr, 0);
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, rawJsonUtf8.c_str(), (int)rawJsonUtf8.size(), wide.data(), len);
    auto* msg = new std::wstring(std::wstring(L"{\"type\":\"") + type + L"\",\"data\":" + wide + L"}");
    PostMessageW(g_hwnd, WM_LIVE_DATA_JSON, 0, (LPARAM)msg);
}

static void FetchLiveData()
{
    const std::wstring ts = std::to_wstring((long long)time(nullptr));
    const std::wstring base = L"https://wow-hc.com/json/";
    const std::wstring qs   = L"?api_version=126&front_realm=1&_=" + ts;
    PostLiveDataMsg(L"serverStats", HttpGetSimple(base + L"server-stats.json" + qs));
    PostLiveDataMsg(L"areasData",   HttpGetSimple(base + L"areas.json"        + qs));
    PostLiveDataMsg(L"newsData",    HttpGetSimple(base + L"last-news.json"     + qs));
}

static bool HttpDownload(const std::wstring& url, const std::wstring& dest,
    const std::function<void(DWORD64, DWORD64)>& progress = nullptr)
{
    HINTERNET hSess = OpenSession();
    if (!hSess) return false;
    HINTERNET hConn = nullptr;
    HINTERNET hReq  = MakeRequest(hSess, url, hConn);
    if (!hReq) { WinHttpCloseHandle(hSess); return false; }
    WinHttpAddRequestHeaders(hReq,
        L"Cache-Control: no-cache\r\nPragma: no-cache",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    bool ok = false;
    bool sent = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    bool recvd = sent && WinHttpReceiveResponse(hReq, nullptr);
    if (!sent || !recvd) {
        DWORD err = GetLastError();
        wchar_t msg[128];
        swprintf_s(msg, L"Network error (WinHTTP 0x%08lX).\nCheck your internet connection and try again.", err);
        MessageBoxW(g_hwnd, msg, L"Download Failed", MB_OK | MB_ICONERROR);
    } else {
        DWORD statusCode = 0; DWORD scLen = sizeof(statusCode);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &statusCode, &scLen, nullptr);
        if (statusCode != 200) {
            // Read up to 2KB of the error body for debugging
            std::string errBody;
            DWORD avail = 0, rd = 0;
            while (errBody.size() < 2048 &&
                   WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                std::vector<char> eb(avail + 1);
                WinHttpReadData(hReq, eb.data(), avail, &rd);
                errBody.append(eb.data(), rd);
            }
            wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
            std::wstring logPath = std::wstring(tmp) + L"wowhc_debug.log";
            if (FILE* f = _wfopen(logPath.c_str(), L"a")) {
                fprintf(f, "URL: %ls\nHTTP %lu\n%s\n---\n",
                    url.c_str(), statusCode, errBody.c_str());
                fclose(f);
            }
            MessageBoxW(g_hwnd,
                (L"HTTP error " + std::to_wstring(statusCode) +
                 L"\nCheck: " + logPath).c_str(),
                L"Download Failed", MB_OK | MB_ICONERROR);
        }
        if (statusCode == 200) {
        wchar_t lenBuf[32] = {}; DWORD ls = sizeof(lenBuf);
        DWORD64 total = 0;
        if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH,
                nullptr, lenBuf, &ls, nullptr))
            total = _wtoi64(lenBuf);
        HANDLE hFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD64 downloaded = 0; DWORD read = 0; ok = true;
            std::vector<char> buf(65536);
            while (WinHttpReadData(hReq, buf.data(), (DWORD)buf.size(), &read) && read > 0) {
                DWORD written;
                if (!WriteFile(hFile, buf.data(), read, &written, nullptr)) { ok = false; break; }
                downloaded += read;
                if (progress) progress(downloaded, total);
            }
            CloseHandle(hFile);
            if (!ok) DeleteFileW(dest.c_str());
        }
        } // statusCode == 200
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return ok;
}

// ── Temp path helpers ─────────────────────────────────────────────────────────
static std::wstring TempFile(const std::wstring& name)
{
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + name;
}
// Large downloads go inside the install path so they don't fill the system drive.
// Falls back to system temp when no install path is set yet.
static std::wstring InstallTempFile(const std::wstring& name)
{
    if (!g_installPath.empty()) return g_installPath + L"\\" + name;
    return TempFile(name);
}

// ── Write a PS script to disk (UTF-8 with BOM) ─────────────────────────────────
static bool WritePsScript(const std::wstring& path, const std::wstring& script)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    static const BYTE bom[] = {0xEF, 0xBB, 0xBF};
    DWORD w;
    WriteFile(h, bom, 3, &w, nullptr);
    int len = WideCharToMultiByte(CP_UTF8, 0, script.c_str(), (int)script.size(),
        nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        std::vector<char> buf(len);
        WideCharToMultiByte(CP_UTF8, 0, script.c_str(), (int)script.size(),
            buf.data(), len, nullptr, nullptr);
        WriteFile(h, buf.data(), len, &w, nullptr);
    }
    CloseHandle(h);
    return true;
}

// ── ZIP extraction with progress via PowerShell ────────────────────────────────
static bool ExtractZipSmart(const std::wstring& zip, const std::wstring& destDir,
    bool stripTopLevel, const std::function<void(int)>& onProgress)
{
    auto escPS = [](const std::wstring& s) {
        std::wstring r;
        for (wchar_t c : s) { if (c == L'\'') r += L"''"; else r += c; }
        return r;
    };

    std::wstring strip = stripTopLevel ? L"$true" : L"$false";
    std::wstring script =
        L"Add-Type -AN System.IO.Compression.FileSystem\r\n"
        L"try {\r\n"
        L"  $z=[System.IO.Compression.ZipFile]::OpenRead('" + escPS(zip) + L"')\r\n"
        L"  $en=@($z.Entries|Where-Object{$_.Name -ne ''})\r\n"
        L"  $n=[Math]::Max($en.Count,1)\r\n"
        L"  $pfx=''\r\n"
        L"  if(" + strip + L"){\r\n"
        L"    $tops=@($z.Entries|ForEach-Object{($_.FullName -split '/')[0]}|Select-Object -Unique)\r\n"
        L"    if($tops.Count -eq 1 -and $z.Entries[0].FullName.Contains('/')){\r\n"
        L"      $pfx=[string]$tops[0]+'/'\r\n"
        L"    }\r\n"
        L"  }\r\n"
        L"  $i=0\r\n"
        L"  foreach($e in $en){\r\n"
        L"    $rel=if($pfx -and $e.FullName.StartsWith($pfx)){$e.FullName.Substring($pfx.Length)}else{$e.FullName}\r\n"
        L"    if($rel){\r\n"
        L"      $d=[System.IO.Path]::Combine('" + escPS(destDir) + L"',$rel.Replace('/','\\'))\r\n"
        L"      $dir=[System.IO.Path]::GetDirectoryName($d)\r\n"
        L"      if($dir){[System.IO.Directory]::CreateDirectory($dir)|Out-Null}\r\n"
        L"      [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e,$d,$true)\r\n"
        L"    }\r\n"
        L"    $i++\r\n"
        L"    [Console]::WriteLine([int]($i*100/$n))\r\n"
        L"  }\r\n"
        L"  $z.Dispose()\r\n"
        L"} catch { [Console]::Error.WriteLine($_); exit 1 }\r\n";

    std::wstring scriptPath = TempFile(L"launcher_extract.ps1");
    if (!WritePsScript(scriptPath, script)) return false;

    std::wstring cmd = L"powershell.exe -NonInteractive -ExecutionPolicy Bypass -File \""
        + scriptPath + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);

    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = {}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DeleteFileW(scriptPath.c_str());
        return false;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;

    PROCESS_INFORMATION pi = {};
    bool started = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
    CloseHandle(hWrite);

    if (!started) {
        CloseHandle(hRead);
        DeleteFileW(scriptPath.c_str());
        return false;
    }

    std::string lineBuf;
    char readBuf[256];
    DWORD bytesRead;
    while (ReadFile(hRead, readBuf, sizeof(readBuf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        readBuf[bytesRead] = '\0';
        lineBuf += readBuf;
        size_t pos;
        while ((pos = lineBuf.find('\n')) != std::string::npos) {
            std::string line = lineBuf.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && onProgress) {
                try { onProgress(std::stoi(line)); } catch (...) {}
            }
            lineBuf = lineBuf.substr(pos + 1);
        }
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    DeleteFileW(scriptPath.c_str());
    return exitCode == 0;
}

// ── Directory helpers for transfer ────────────────────────────────────────────
static void DeleteDirRecursive(const std::wstring& path)
{
    if (path.empty()) return;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::vector<wchar_t> from(path.begin(), path.end());
    from.push_back(0); from.push_back(0);
    SHFILEOPSTRUCTW sfo = {};
    sfo.wFunc  = FO_DELETE;
    sfo.pFrom  = from.data();
    sfo.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&sfo);
}

static int CountFiles(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            count += CountFiles(dir + L"\\" + fd.cFileName);
        else
            ++count;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return count;
}

static bool CopyDirRecursive(const std::wstring& src, const std::wstring& dst,
    int& copied, int total, const std::function<void(int)>& progress)
{
    CreateDirectoryW(dst.c_str(), nullptr);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring srcItem = src + L"\\" + fd.cFileName;
        std::wstring dstItem = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CopyDirRecursive(srcItem, dstItem, copied, total, progress)) {
                FindClose(h);
                return false;
            }
        } else {
            if (!CopyFileW(srcItem.c_str(), dstItem.c_str(), FALSE)) {
                FindClose(h);
                return false;
            }
            ++copied;
            if (total > 0 && progress) progress(copied * 100 / total);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

// ── Process helpers ────────────────────────────────────────────────────────────
static bool IsProcessRunning(const wchar_t* exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe))
        do { if (_wcsicmp(pe.szExeFile, exeName) == 0) { found = true; break; } }
        while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return found;
}

static void KillProcess(const wchar_t* exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

// Polls up to timeoutMs for a process named exeName whose parent PID is parentPid.
// Returns the child PID, or 0 on timeout.
static DWORD WaitForChildProcess(DWORD parentPid, const wchar_t* exeName, DWORD timeoutMs)
{
    for (DWORD elapsed = 0; elapsed < timeoutMs; elapsed += 500) {
        Sleep(500);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) continue;
        PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
        DWORD found = 0;
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, exeName) == 0 &&
                    pe.th32ParentProcessID == parentPid) {
                    found = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        if (found) return found;
    }
    return 0;
}

static bool IsPortInUse(int port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { WSACleanup(); return false; }
    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)port);
    bool inUse = (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR);
    closesocket(s);
    WSACleanup();
    return inUse;
}

// Ensures SET portal "127.0.0.1" in _classic_era_\WTF\Config.wtf so HermesProxy intercepts traffic.
static void PatchConfigWtfPortal(const std::wstring& clientPath)
{
    std::wstring path = clientPath + L"\\_classic_era_\\WTF\\Config.wtf";
    // Read existing file
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::string content;
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(hf, nullptr);
        if (sz && sz != INVALID_FILE_SIZE) {
            content.resize(sz);
            DWORD rd = 0;
            ReadFile(hf, content.data(), sz, &rd, nullptr);
            content.resize(rd);
        }
        CloseHandle(hf);
    }

    // Replace or append SET portal line (case-insensitive key match)
    const std::string target = "SET portal \"127.0.0.1\"";
    std::string out;
    out.reserve(content.size() + 32);
    bool found = false;
    size_t i = 0;
    while (i < content.size()) {
        size_t nl = content.find('\n', i);
        std::string line = content.substr(i, nl == std::string::npos ? std::string::npos : nl - i + 1);
        i = (nl == std::string::npos) ? content.size() : nl + 1;
        // Check if this line is "SET portal ..."
        size_t s = line.find_first_not_of(" \t\r\n");
        if (s != std::string::npos && _strnicmp(line.c_str() + s, "SET portal ", 11) == 0) {
            // Replace with correct value, preserve line ending
            std::string ending = (!line.empty() && line.back() == '\n')
                ? (line.size() >= 2 && line[line.size()-2] == '\r' ? "\r\n" : "\n") : "";
            out += target + ending;
            found = true;
        } else {
            out += line;
        }
    }
    if (!found) {
        if (!out.empty() && out.back() != '\n') out += "\r\n";
        out += target + "\r\n";
    }

    // Write back
    hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(hf, out.data(), (DWORD)out.size(), &wr, nullptr);
        CloseHandle(hf);
    }
}

// Protects key files from Battle.net auto-updates by toggling their read-only attribute.
// Called with readOnly=true before launch and readOnly=false after game exits.
static void SetClientFilesReadOnly(const std::wstring& clientPath, bool readOnly)
{
    const wchar_t* suffixes[] = {
        L"\\.build.info",
        L"\\_classic_era_\\.build.info",
        L"\\_classic_era_\\WowClassic.exe",
    };
    for (auto* suffix : suffixes) {
        std::wstring f = clientPath + suffix;
        DWORD attr = GetFileAttributesW(f.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) continue;
        if (readOnly)
            attr |= FILE_ATTRIBUTE_READONLY;
        else
            attr &= ~FILE_ATTRIBUTE_READONLY;
        SetFileAttributesW(f.c_str(), attr);
    }
}

// Launches exe and returns its process handle (caller must CloseHandle), or nullptr on failure.
static HANDLE LaunchExeGetHandle(const std::wstring& exe, const std::wstring& args)
{
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!args.empty()) { cmd += L' '; cmd += args; }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    std::wstring dir = exe.substr(0, exe.rfind(L'\\'));
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0,
            nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        return pi.hProcess;
    }
    DWORD err = GetLastError();
    // ERROR_ELEVATION_REQUIRED (740): exe has requireAdministrator manifest — retry via ShellExecuteEx which triggers UAC
    if (err == ERROR_ELEVATION_REQUIRED) {
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = exe.c_str();
        sei.lpParameters = args.empty() ? nullptr : args.c_str();
        sei.lpDirectory  = dir.empty() ? nullptr : dir.c_str();
        sei.nShow  = SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) {
            return sei.hProcess; // may be nullptr; caller handles that
        }
        err = GetLastError();
    }
    wchar_t msg[512];
    swprintf_s(msg, L"Failed to launch:\n%s\n\nError code: %lu\n\n"
        L"If this keeps happening, try right-clicking the exe and selecting 'Run as administrator'.",
        exe.c_str(), err);
    MessageBoxW(g_hwnd, msg, L"Launch Error", MB_OK | MB_ICONERROR);
    return nullptr;
}

static void LaunchExe(const std::wstring& exe, const std::wstring& args)
{
    HANDLE h = LaunchExeGetHandle(exe, args);
    if (h) CloseHandle(h);
}

static void LaunchHermes(const std::wstring& exe)
{
    LaunchExe(exe, L"");
}

static COLORREF Ansi256ToColor(int idx)
{
    if (idx < 16)  return g_ansiColors[idx];
    if (idx >= 232) { int v = 8 + (idx - 232) * 10; return RGB(v, v, v); }
    idx -= 16;
    auto ch = [](int x) -> int { return x ? 55 + x * 40 : 0; };
    return RGB(ch(idx / 36), ch((idx / 6) % 6), ch(idx % 6));
}

static void AppendAnsiLine(HWND hwnd, const std::wstring& line)
{
    auto setColor = [hwnd](COLORREF c) {
        CHARFORMAT2W cf = {};
        cf.cbSize     = sizeof(cf);
        cf.dwMask     = CFM_COLOR | CFM_EFFECTS;
        cf.crTextColor = c;
        cf.dwEffects  = 0;
        SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    };

    // Prepend newline separator if not the first line
    LRESULT textLen = SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(hwnd, EM_SETSEL, textLen, textLen);
    if (textLen > 0) {
        setColor(CLR_TEXT);
        SendMessageW(hwnd, EM_REPLACESEL, FALSE, (LPARAM)L"\n");
    }

    COLORREF curColor = CLR_TEXT;
    std::wstring buf;
    size_t i = 0;

    auto flush = [&]() {
        if (buf.empty()) return;
        SendMessageW(hwnd, EM_SETSEL, -1, -1);
        setColor(curColor);
        SendMessageW(hwnd, EM_REPLACESEL, FALSE, (LPARAM)buf.c_str());
        buf.clear();
    };

    while (i < line.size()) {
        if (line[i] == L'\x1b' && i + 1 < line.size() && line[i + 1] == L'[') {
            flush();
            i += 2;
            std::wstring params;
            while (i < line.size() && (iswdigit(line[i]) || line[i] == L';'))
                params += line[i++];
            if (i < line.size()) ++i; // skip command letter

            std::vector<int> nums;
            int cur = 0; bool any = false;
            for (wchar_t c : params) {
                if (c == L';') { nums.push_back(any ? cur : 0); cur = 0; any = false; }
                else           { cur = cur * 10 + (c - L'0'); any = true; }
            }
            nums.push_back(any ? cur : 0);

            for (size_t n = 0; n < nums.size(); ++n) {
                int code = nums[n];
                if      (code == 0)                curColor = CLR_TEXT;
                else if (code == 39)               curColor = CLR_TEXT;
                else if (code >= 30 && code <= 37) curColor = g_ansiColors[code - 30];
                else if (code >= 90 && code <= 97) curColor = g_ansiColors[code - 90 + 8];
                else if (code == 38 && n + 1 < nums.size()) {
                    if (nums[n+1] == 2 && n + 4 < nums.size()) {
                        curColor = RGB(nums[n+2], nums[n+3], nums[n+4]); n += 4;
                    } else if (nums[n+1] == 5 && n + 2 < nums.size()) {
                        curColor = Ansi256ToColor(nums[n+2]); n += 2;
                    }
                }
            }
        } else {
            buf += line[i++];
        }
    }
    flush();
    SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
}

static std::string StripAnsi(const std::string& in)
{
    std::string out; out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ) {
        if (in[i] == '\x1b' && i + 1 < in.size() && in[i+1] == '[') {
            i += 2;
            while (i < in.size() && !isalpha((unsigned char)in[i])) ++i;
            if (i < in.size()) ++i;
        } else { out += in[i++]; }
    }
    return out;
}

static std::wstring StripAnsiW(const std::wstring& in)
{
    std::wstring out; out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ) {
        if (in[i] == L'\x1b' && i + 1 < in.size() && in[i+1] == L'[') {
            i += 2;
            while (i < in.size() && !iswalpha(in[i])) ++i;
            if (i < in.size()) ++i;
        } else { out += in[i++]; }
    }
    return out;
}

static void LaunchHermesWithPipe(const std::wstring& exe)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hR = nullptr, hW = nullptr;
    if (!CreatePipe(&hR, &hW, &sa, 0)) {
        LaunchExe(exe, L"");
        return;
    }
    SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hW;
    si.hStdError  = hW;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + exe + L"\"";
    std::wstring dir = exe.substr(0, exe.rfind(L'\\'));
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW, nullptr,
                             dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(hW);

    if (!ok) {
        CloseHandle(hR);
        LaunchExe(exe, L"");
        PostMessageW(g_hwnd, WM_HERMES_LINE, 0,
            (LPARAM)(new std::wstring(L"[pipe launch failed - started externally]")));
        return;
    }

    if (g_hermesProcess)  { CloseHandle(g_hermesProcess); }
    if (g_hermesPipeRead) { CloseHandle(g_hermesPipeRead); }
    g_hermesProcess  = pi.hProcess;
    g_hermesPipeRead = hR;
    CloseHandle(pi.hThread);

    PostMessageW(g_hwnd, WM_HERMES_LINE, 0,
        (LPARAM)(new std::wstring(L"--- HermesProxy starting ---")));

    HANDLE hReadCopy = hR;
    std::thread([hReadCopy]() {
        char buf[1024]; DWORD nRead;
        std::string partial;
        while (ReadFile(hReadCopy, buf, sizeof(buf) - 1, &nRead, nullptr) && nRead > 0) {
            buf[nRead] = 0;
            partial += std::string(buf, nRead);
            size_t pos;
            while ((pos = partial.find('\n')) != std::string::npos) {
                std::string line = partial.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) {
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
                    if (wlen > 1) {
                        auto* ws = new std::wstring(wlen - 1, L'\0');
                        MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, ws->data(), wlen);
                        PostMessageW(g_hwnd, WM_HERMES_LINE, 0, (LPARAM)ws);
                    }
                }
                partial = partial.substr(pos + 1);
            }
        }
    }).detach();
}

// ── Size / speed formatting ────────────────────────────────────────────────────
static std::wstring FmtBytes(DWORD64 bytes)
{
    wchar_t buf[32];
    if (bytes >= 1024ULL * 1024 * 1024)
        swprintf_s(buf, L"%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024 * 1024)
        swprintf_s(buf, L"%.1f MB", bytes / (1024.0 * 1024.0));
    else
        swprintf_s(buf, L"%.0f KB", bytes / 1024.0);
    return buf;
}


static std::wstring FmtSpeed(DWORD64 bytesPerSec)
{
    wchar_t buf[32];
    if (bytesPerSec >= 1024 * 1024)
        swprintf_s(buf, L"%.1f MB/s", bytesPerSec / (1024.0 * 1024.0));
    else
        swprintf_s(buf, L"%.0f KB/s", bytesPerSec / 1024.0);
    return buf;
}

// ── GDI+ helpers ──────────────────────────────────────────────────────────────
static Gdiplus::Bitmap* LoadPngFromResource(int resId)
{
    HRSRC hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(resId), L"PNG");
    if (!hRes) return nullptr;
    HGLOBAL hMem = LoadResource(g_hInst, hRes);
    if (!hMem) return nullptr;
    void* pData = LockResource(hMem);
    DWORD size  = SizeofResource(g_hInst, hRes);
    if (!pData || !size) return nullptr;
    IStream* pStream = SHCreateMemStream((const BYTE*)pData, size);
    if (!pStream) return nullptr;
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return nullptr; }
    return bmp;
}

static HICON CreateIconFromPng(int resId, int size)
{
    std::unique_ptr<Gdiplus::Bitmap> src(LoadPngFromResource(resId));
    if (!src) return nullptr;
    Gdiplus::Bitmap scaled(size, size, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&scaled);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.DrawImage(src.get(), 0, 0, size, size);
    }
    HICON hIcon = nullptr;
    scaled.GetHICON(&hIcon);
    return hIcon;
}

// ── WebView2 bridge ────────────────────────────────────────────────────────────

static std::wstring JsonEscW(const std::wstring& s)
{
    std::wstring r; r.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
            case L'"':    r += L"\\\"";    break;
            case L'\\':   r += L"\\\\";    break;
            case L'\n':   r += L"\\n";     break;
            case L'\r':                    break;
            case L'\x1b': r += L"\\u001b"; break;
            default:      r += c;
        }
    }
    return r;
}

static void PostStateToWebView()
{
    if (!g_webview || !g_wvReady) return;

    bool isInstalled  = g_playReady.load();
    bool isRecording  = RB_IsRunning();
    bool workerBusy   = g_workerBusy.load() || g_ffmpegBusy.load();
    bool isLaunching  = g_isLaunching.load();
    // playEnabled: never true when worker is running or game is launching
    bool playEnabled  = !g_clientPath.empty() && !workerBusy && !isLaunching &&
                        (!g_clientInstalled.load() || g_playReady.load());

    const char* lv = LAUNCHER_VERSION_STR;
    std::wstring verLauncher(lv, lv + strlen(lv));
    std::wstring verHermes = GetLocalHermesVersion();
    std::wstring verAddon  = ReadAddonTocVersion();
    if (!verAddon.empty() && verAddon[0] != L'v') verAddon = L"v" + verAddon;
    std::wstring verClient = (g_clientType == CT_114) ? L"v1.14.2"
                           : (g_clientType == CT_112) ? L"v1.12.1" : L"";

    wchar_t json[4096];
    swprintf_s(json,
        L"{\"type\":\"state\","
        L"\"status\":\"%s\","
        L"\"progress\":%d,"
        L"\"installPath\":\"%s\","
        L"\"isInstalled\":%s,"
        L"\"isRecording\":%s,"
        L"\"canSaveReplay\":%s,"
        L"\"playEnabled\":%s,"
        L"\"workerBusy\":%s,"
        L"\"isLaunching\":%s,"
        L"\"realmIndex\":%d,"
        L"\"clientType\":%d,"
        L"\"showConsole\":%s,"
        L"\"showRecordingNotifications\":%s,"
        L"\"versions\":{"
          L"\"launcher\":\"%s\","
          L"\"hermes\":\"%s\","
          L"\"addon\":\"%s\","
          L"\"client\":\"%s\""
        L"}}",
        JsonEscW(g_currentStatus).c_str(),
        g_currentProgress,
        JsonEscW(g_installPath.empty() ? g_clientPath : g_installPath).c_str(),
        isInstalled  ? L"true" : L"false",
        isRecording  ? L"true" : L"false",
        isRecording  ? L"true" : L"false",
        playEnabled  ? L"true" : L"false",
        workerBusy   ? L"true" : L"false",
        isLaunching  ? L"true" : L"false",
        g_realmIndex,
        (int)g_clientType,
        g_showConsole ? L"true" : L"false",
        g_showRecordingNotifications ? L"true" : L"false",
        JsonEscW(verLauncher).c_str(),
        JsonEscW(verHermes).c_str(),
        JsonEscW(verAddon).c_str(),
        JsonEscW(verClient).c_str());

    static int s_postCount = 0;
    AppendLog(L"PostStateToWebView #%d: installPath='%s' clientType=%d playReady=%s json=%s",
        ++s_postCount,
        (g_installPath.empty() ? g_clientPath : g_installPath).c_str(),
        (int)g_clientType,
        g_playReady.load() ? L"true" : L"false",
        json);
    g_webview->PostWebMessageAsJson(json);
}

static void PostHermesLineToWebView(const std::wstring& line)
{
    if (!g_webview || !g_wvReady) return;
    std::wstring json = L"{\"type\":\"hermesLine\",\"text\":\"" + JsonEscW(line) + L"\"}";
    g_webview->PostWebMessageAsJson(json.c_str());
}

// ── React modal bridge ────────────────────────────────────────────────────────
static void PostShowModal(const wchar_t* type)
{
    if (!g_webview || !g_wvReady) return;
    std::wstring j = std::wstring(L"{\"type\":\"showModal\",\"modal\":\"") + type + L"\"}";
    g_webview->PostWebMessageAsJson(j.c_str());
}

static void PostRecordSettingsStateToWebView()
{
    if (!g_webview || !g_wvReady) return;
    const ReplaySettings& s = RB_GetSettings();
    auto monitors = RB_EnumMonitors();

    std::wstring monsJson = L"[";
    for (int i = 0; i < (int)monitors.size(); i++) {
        if (i > 0) monsJson += L",";
        monsJson += L"{\"index\":" + std::to_wstring(i)
                  + L",\"name\":\"" + JsonEscW(monitors[i].name) + L"\"}";
    }
    monsJson += L"]";

    std::wstring json =
        L"{\"type\":\"recordSettingsState\","
        L"\"monitors\":" + monsJson + L","
        L"\"monitorIndex\":" + std::to_wstring(s.monitorIndex) + L","
        L"\"minutes\":"      + std::to_wstring(s.minutes)      + L","
        L"\"fps\":"          + std::to_wstring(s.fps)          + L","
        L"\"saveFolder\":\""      + JsonEscW(s.saveFolder)     + L"\","
        L"\"promptSaveOnStop\":"  + (s.promptSaveOnStop ? L"true" : L"false") + L","
        L"\"autoStartOnPlay\":"   + (s.autoStartOnPlay  ? L"true" : L"false") + L","
        L"\"startStopVK\":"   + std::to_wstring(s.startStopVK)   + L","
        L"\"startStopMods\":" + std::to_wstring(s.startStopMods) + L","
        L"\"saveVK\":"   + std::to_wstring(s.saveVK)   + L","
        L"\"saveMods\":" + std::to_wstring(s.saveMods) + L"}";

    g_webview->PostWebMessageAsJson(json.c_str());
}

static void PostGeneralSettingsStateToWebView()
{
    if (!g_webview || !g_wvReady) return;
    wchar_t json[256];
    swprintf_s(json,
        L"{\"type\":\"generalSettingsState\","
        L"\"showRecordingNotifications\":%s}",
        g_showRecordingNotifications ? L"true" : L"false");
    g_webview->PostWebMessageAsJson(json);
}

// Runs a nested Win32 message pump (same pattern as native modal dialogs) until
// JS sends a modal response action that sets g_reactModalDone.
// Waits for the page to be ready first if called before JS fires "ready".
// modalType: if non-null, PostShowModal is called after confirming the page is ready,
// eliminating the race where the caller posts before JS has attached its listener.
static int WaitForModalResponse(const wchar_t* modalType = nullptr)
{
    if (!g_wvPageReady) {
        MSG m;
        while (!g_wvPageReady && GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m); DispatchMessageW(&m);
        }
    }
    if (modalType) PostShowModal(modalType);
    g_reactModalDone   = false;
    g_reactModalResult = 0;
    MSG m;
    while (!g_reactModalDone) {
        BOOL r = GetMessageW(&m, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage(0); break; }
        if (r < 0) break;
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    return g_reactModalResult;
}

static void HandleWebMessage(HWND hwnd, const std::string& jsonStr);  // forward decl

static bool CheckWebView2Runtime()
{
    wchar_t* ver = nullptr;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &ver);
    bool ok = SUCCEEDED(hr) && ver;
    if (ver) CoTaskMemFree(ver);
    return ok;
}

static bool ShowWebView2InstallPrompt(HWND hwnd)
{
    int r = MessageBoxW(hwnd,
        L"WoW HC Launcher requires the Microsoft WebView2 runtime,\n"
        L"which is not installed on this system.\n\n"
        L"Click OK to download and install it automatically (~2 MB),\n"
        L"then restart the launcher.\n\n"
        L"Click Cancel to exit.",
        L"WebView2 Runtime Required",
        MB_OKCANCEL | MB_ICONINFORMATION);
    if (r != IDOK) return false;

    // ── Progress window ──────────────────────────────────────────────────────
    static const wchar_t kPrgCls[] = L"WV2ProgressWnd";
    {
        WNDCLASSW wc    = {};
        wc.lpfnWndProc  = DefWindowProcW;
        wc.hInstance    = GetModuleHandleW(nullptr);
        wc.hbrBackground= (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor      = LoadCursor(nullptr, IDC_WAIT);
        wc.lpszClassName= kPrgCls;
        RegisterClassW(&wc); // ignore failure if already registered
    }

    int dlgW = MulDiv(380, g_dpi, 96);
    int dlgH = MulDiv(112, g_dpi, 96);
    HWND hDlg = CreateWindowExW(0, kPrgCls, L"Installing WebView2 Runtime",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        (GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2,
        dlgW, dlgH,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    HWND hLabel = nullptr, hBar = nullptr;
    if (hDlg) {
        // Gray out the close button so the user can't cancel mid-install
        HMENU hSys = GetSystemMenu(hDlg, FALSE);
        if (hSys) EnableMenuItem(hSys, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);

        RECT cr; GetClientRect(hDlg, &cr);
        int pad  = MulDiv(12, g_dpi, 96);
        int lblH = MulDiv(16, g_dpi, 96);
        int barH = MulDiv(20, g_dpi, 96);
        int cW   = cr.right - cr.left;

        hLabel = CreateWindowW(L"STATIC", L"Connecting...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, pad, cW - 2*pad, lblH,
            hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessage(hLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);

        hBar = CreateWindowW(PROGRESS_CLASS, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            pad, pad + lblH + MulDiv(6, g_dpi, 96), cW - 2*pad, barH,
            hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessage(hBar, PBM_SETRANGE32, 0, 100);

        ShowWindow(hDlg, SW_SHOW);
        UpdateWindow(hDlg);
    }

    auto Pump = [&]() {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    };
    auto SetLabel = [&](const wchar_t* s) {
        if (hLabel) SetWindowTextW(hLabel, s);
        Pump();
    };
    auto SetPct = [&](int pct) {
        if (hBar) SendMessage(hBar, PBM_SETPOS, pct, 0);
        Pump();
    };
    bool marqueeOn = false;
    auto StartMarquee = [&]() {
        if (!hBar || marqueeOn) return;
        SetWindowLongPtrW(hBar, GWL_STYLE,
            GetWindowLongPtrW(hBar, GWL_STYLE) | PBS_MARQUEE);
        SendMessage(hBar, PBM_SETMARQUEE, TRUE, 40);
        marqueeOn = true;
        Pump();
    };
    auto StopMarquee = [&]() {
        if (!hBar || !marqueeOn) return;
        SendMessage(hBar, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongPtrW(hBar, GWL_STYLE,
            GetWindowLongPtrW(hBar, GWL_STYLE) & ~PBS_MARQUEE);
        InvalidateRect(hBar, nullptr, TRUE);
        UpdateWindow(hBar);
        marqueeOn = false;
        Pump();
    };

    // ── Download ─────────────────────────────────────────────────────────────
    SetLabel(L"Downloading WebView2 installer...");

    std::wstring tmp = TempFile(L"MicrosoftEdgeWebview2Setup.exe");
    bool downloaded = false;
    {
        HINTERNET hSess = OpenSession();
        if (hSess) {
            HINTERNET hConn = nullptr;
            HINTERNET hReq  = MakeRequest(hSess,
                L"https://go.microsoft.com/fwlink/p/?LinkId=2124703", hConn);
            if (hReq) {
                if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                    && WinHttpReceiveResponse(hReq, nullptr)) {

                    // Try to get Content-Length for percentage-based progress
                    DWORD totalBytes = 0, sz = sizeof(totalBytes);
                    WinHttpQueryHeaders(hReq,
                        WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &totalBytes, &sz, WINHTTP_NO_HEADER_INDEX);
                    if (totalBytes == 0)
                        StartMarquee(); // no Content-Length: use marquee

                    HANDLE hf = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hf != INVALID_HANDLE_VALUE) {
                        std::vector<BYTE> buf(65536);
                        DWORD rd = 0, totalRead = 0;
                        while (WinHttpReadData(hReq, buf.data(), (DWORD)buf.size(), &rd) && rd) {
                            DWORD wr;
                            WriteFile(hf, buf.data(), rd, &wr, nullptr);
                            totalRead += rd;
                            if (totalBytes > 0)
                                SetPct((int)((DWORD64)totalRead * 100 / totalBytes));
                        }
                        CloseHandle(hf);
                        downloaded = true;
                    }
                }
                WinHttpCloseHandle(hReq);
            }
            if (hConn) WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSess);
        }
    }

    if (!downloaded) {
        if (hDlg) DestroyWindow(hDlg);
        MessageBoxW(hwnd,
            L"Download failed. Please install WebView2 manually:\n"
            L"https://developer.microsoft.com/microsoft-edge/webview2/",
            L"Download Failed", MB_OK | MB_ICONERROR);
        return false;
    }

    // ── Install ──────────────────────────────────────────────────────────────
    StopMarquee();
    SetPct(100);
    SetLabel(L"Installing WebView2 Runtime, please wait a few minutes...");
    StartMarquee();

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"open";
    sei.lpFile       = tmp.c_str();
    sei.lpParameters = L"/silent /install";
    sei.nShow        = SW_SHOW;
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        DWORD wait;
        do {
            wait = WaitForSingleObject(sei.hProcess, 100);
            Pump();
        } while (wait == WAIT_TIMEOUT);
        CloseHandle(sei.hProcess);
    }
    DeleteFileW(tmp.c_str());

    if (hDlg) DestroyWindow(hDlg);

    // Relaunch the launcher so it picks up the newly installed WebView2 runtime.
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    HINSTANCE h = ShellExecuteW(nullptr, L"open", exePath, nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)h <= 32) {
        MessageBoxW(hwnd,
            L"WebView2 has been installed.\nPlease restart the launcher.",
            L"Restart Required", MB_OK | MB_ICONINFORMATION);
    }
    return false;
}

static std::wstring GetExeDir(); // forward declaration (defined in FFmpeg section below)

static void InitWebView2(HWND hwnd)
{
    std::wstring dataDir  = g_configDir + L"\\webview2_data";
    std::wstring exeDir   = GetExeDir();

    // Dev: prefer source-tree ../ui so JSX edits are visible after a plain restart.
    // Release: use AppData ui/ (migrated there from the Full ZIP on first run).
    std::wstring uiFolder;
    {
        wchar_t full[MAX_PATH] = {};
        std::wstring candidate = exeDir + L"\\..\\ui";
        if (GetFullPathNameW(candidate.c_str(), MAX_PATH, full, nullptr) &&
            GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES)
            uiFolder = full;
    }
    if (uiFolder.empty())
        uiFolder = g_configDir + L"\\ui";

    auto envCb = Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hwnd, uiFolder](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) return hr;
            env->CreateCoreWebView2Controller(hwnd,
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [hwnd, uiFolder](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                        if (FAILED(hr) || !ctrl) return hr;

                        g_wvCtrl = ctrl;
                        ctrl->get_CoreWebView2(g_webview.GetAddressOf());

                        RECT rc; GetClientRect(hwnd, &rc);
                        ctrl->put_Bounds(rc);
                        ctrl->put_IsVisible(TRUE);
                        // Prevent inherited Edge profile zoom from cropping the fixed-size UI
                        ctrl->put_ZoomFactor(1.0);

                        // Settings
                        Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                        if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
                            settings->put_AreDevToolsEnabled(strstr(LAUNCHER_VERSION_STR, "-dev") ? TRUE : FALSE);
                            settings->put_AreDefaultContextMenusEnabled(FALSE);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_IsZoomControlEnabled(FALSE);
                        }

                        // Map virtual host → ui/ folder (AppData in release, ../ui in dev)
                        Microsoft::WRL::ComPtr<ICoreWebView2_3> wv3;
                        g_webview.As(&wv3);
                        if (wv3) {
                            wv3->SetVirtualHostNameToFolderMapping(
                                L"wow-hc-client-launcher.local", uiFolder.c_str(),
                                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                        }
                        std::wstring navUrl = L"https://wow-hc-client-launcher.local/index.html";
                        // Append ?v=VERSION so each release gets a fresh cache entry for
                        // index.html and (via JS in the page) for app.jsx / data.jsx too.
                        {
                            const char* ver = LAUNCHER_VERSION_STR;
                            std::wstring verW(ver, ver + strlen(ver));
                            navUrl += L"?v=" + verW;
                        }

                        // Web message handler
                        EventRegistrationToken tok;
                        g_webview->add_WebMessageReceived(
                            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [hwnd](ICoreWebView2* /*sender*/,
                                       ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                    LPWSTR raw = nullptr;
                                    args->TryGetWebMessageAsString(&raw);
                                    if (raw) {
                                        int sz = WideCharToMultiByte(CP_UTF8, 0, raw, -1,
                                                    nullptr, 0, nullptr, nullptr);
                                        std::string narrow(sz > 1 ? sz - 1 : 0, '\0');
                                        WideCharToMultiByte(CP_UTF8, 0, raw, -1,
                                                    narrow.data(), sz, nullptr, nullptr);
                                        CoTaskMemFree(raw);
                                        HandleWebMessage(hwnd, narrow);
                                    }
                                    return S_OK;
                                }).Get(), &tok);

                        g_wvReady = true;
                        g_webview->Navigate(navUrl.c_str());
                        return S_OK;
                    }).Get());
            return S_OK;
        });

    CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), nullptr, envCb.Get());
}

// ── UI helpers ─────────────────────────────────────────────────────────────────
static void PostStatus(WorkerStatus s)
{
    g_currentStatus = STATUS_TEXT[s];
    PostMessageW(g_hwnd, WM_WORKER_STATUS, (WPARAM)s, 0);
}
static void PostPct(int pct)
{
    PostMessageW(g_hwnd, WM_WORKER_PROGRESS, (WPARAM)pct, 0);
}
static void PostText(std::wstring text)
{
    PostMessageW(g_hwnd, WM_WORKER_TEXT, 0, (LPARAM)(new std::wstring(std::move(text))));
}

static void PostStateToWebView(); // forward declaration

static void RefreshTransferButton() { /* no Win32 button — state pushed via PostStateToWebView */ }

static void RefreshPlayButton()     { PostStateToWebView(); }

static void RefreshVersionLabels()  { PostStateToWebView(); }

// ── Recording helpers ─────────────────────────────────────────────────────────
// Returns true if the save folder is set (auto-populates default if not).
// Shows a warning and returns false only when g_clientPath is also unknown.
static bool EnsureRecordingSaveFolder(HWND hwnd)
{
    ReplaySettings s = RB_GetSettings();
    if (!s.saveFolder.empty()) return true;

    if (!g_clientPath.empty()) {
        if (g_clientType == CT_112) {
            std::wstring era = g_clientPath + L"\\_classic_era_";
            s.saveFolder = (GetFileAttributesW(era.c_str()) != INVALID_FILE_ATTRIBUTES)
                ? era + L"\\Videos"
                : g_clientPath + L"\\Videos";
        } else {
            s.saveFolder = g_clientPath + L"\\_classic_era_\\Videos";
        }
        CreateDirectoryW(s.saveFolder.c_str(), nullptr);
        RB_SetSettings(s);
        SaveReplaySettings(s, ConfigPath());
        return true;
    }

    MessageBoxW(hwnd,
        L"No save folder is configured for recordings.\r\n"
        L"Please click the Settings button (\x2699) to set one.",
        L"Recording Save Folder", MB_OK | MB_ICONWARNING);
    return false;
}

// ── Progress helper ────────────────────────────────────────────────────────────
struct DlProgress {
    std::wstring label;
    int          pctMax;
    DWORD64 lastTick  = 0;
    DWORD64 lastBytes = 0;
    DWORD64 speed     = 0;

    void operator()(DWORD64 dl, DWORD64 tot)
    {
        DWORD64 now = GetTickCount64();
        if (lastTick == 0) lastTick = now;
        DWORD64 elapsed = now - lastTick;
        if (elapsed >= 1000) {
            DWORD64 delta = (dl > lastBytes) ? dl - lastBytes : 0;
            speed = delta * 1000 / elapsed;
            lastBytes = dl; lastTick = now;
        }
        if (tot > 0) PostPct((int)(dl * pctMax / tot));
        if (elapsed < 1000 && lastTick != 0 && speed != 0) return;
        std::wstring text = label + L"   ";
        if (tot > 0) {
            wchar_t pctBuf[8];
            swprintf_s(pctBuf, L"%d%%  ", (int)(dl * 100 / tot));
            text += pctBuf;
            text += FmtBytes(dl) + L" / " + FmtBytes(tot);
        } else {
            text += FmtBytes(dl);
        }
        if (speed > 0) text += L"   -   " + FmtSpeed(speed);
        PostText(std::move(text));
    }
};

// ── Launcher version ───────────────────────────────────────────────────────────
static std::wstring GetLauncherVersion()
{
    const char* v = LAUNCHER_VERSION_STR;
    return std::wstring(v, v + strlen(v));
}

static bool IsDevBuild()
{
    return g_devMode || strstr(LAUNCHER_VERSION_STR, "-dev") != nullptr;
}

// ── EXE asset URL (for self-update) ────────────────────────────────────────────
static std::string FindExeAssetUrl(const std::string& json)
{
    size_t pos = 0;
    while (true) {
        size_t found = json.find("\"browser_download_url\"", pos);
        if (found == std::string::npos) break;
        std::string url = JsonString(json.substr(found), "browser_download_url");
        if (!url.empty()) {
            std::string lower = url;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.rfind(".exe") != std::string::npos) return url;
        }
        pos = found + 1;
    }
    return {};
}

static std::string FindFullZipAssetUrl(const std::string& json)
{
    size_t pos = 0;
    while (true) {
        size_t found = json.find("\"browser_download_url\"", pos);
        if (found == std::string::npos) break;
        std::string url = JsonString(json.substr(found), "browser_download_url");
        if (!url.empty()) {
            std::string lower = url;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("full") != std::string::npos && lower.rfind(".zip") != std::string::npos)
                return url;
        }
        pos = found + 1;
    }
    return {};
}

// ── Self-update: rename running EXE, place new one, relaunch ───────────────────
static void ApplyLauncherUpdate(const std::wstring& newExePath)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring oldPath = std::wstring(exePath) + L".old";
    DeleteFileW(oldPath.c_str());
    if (!MoveFileW(exePath, oldPath.c_str())) {
        DeleteFileW(newExePath.c_str());
        MessageBoxW(nullptr, L"Failed to rename the current launcher EXE.\nMake sure the launcher is not running from a read-only location.", L"Update Failed", MB_OK | MB_ICONERROR);
        return;
    }
    if (!MoveFileW(newExePath.c_str(), exePath)) {
        MoveFileW(oldPath.c_str(), exePath);
        MessageBoxW(nullptr, L"Failed to place the new launcher EXE.\nThe update was rolled back.", L"Update Failed", MB_OK | MB_ICONERROR);
        return;
    }
    SaveConfig();
    ShellExecuteW(nullptr, L"open", exePath, nullptr, nullptr, SW_SHOWNORMAL);
    PostQuitMessage(0);
}

// ── Per-component update helpers (callable from worker or timer thread) ─────────
static void RunHermesUpdateCheck()
{
    if (IsDevBuild()) { PostText(L"Fetching from GitHub is disabled in dev mode to prevent reaching rate limits."); return; }
    std::wstring apiUrl = std::wstring(L"https://api.github.com/repos/")
        + HERMES_GH_OWNER + L"/" + HERMES_GH_REPO + L"/releases/latest";
    std::string json = HttpGet(apiUrl);
    if (json.empty()) return;

    std::string tag = JsonString(json, "tag_name");
    std::wstring remoteVer(tag.begin(), tag.end());
    std::wstring localVer = GetLocalHermesVersion();
    if (remoteVer.empty() || !IsNewer(localVer, remoteVer)) return;

    std::wstring payload = remoteVer + L"\n" + localVer;
    LRESULT r = SendMessageW(g_hwnd, WM_ASK_UPDATE, UC_HERMES,
        (LPARAM)(new std::wstring(payload)));
    if (r != IDYES) return;

    std::string assetUrl = FindAssetUrl(json);
    if (assetUrl.empty()) return;

    std::wstring assetW(assetUrl.begin(), assetUrl.end());
    std::wstring tmpZip = InstallTempFile(L"hermes_update.zip");

    PostStatus(WS_DL_HERMES); PostPct(0);
    DlProgress dlHermes{L"Downloading HermesProxy update...", 70};
    bool ok = HttpDownload(assetW, tmpZip,
        [&dlHermes](DWORD64 dl, DWORD64 tot) { dlHermes(dl, tot); });

    if (ok) {
        PostStatus(WS_EX_HERMES);
        // Extract to the directory that should contain HermesProxy.exe.
        // For existing installs this may differ from g_clientPath.
        std::wstring hermesDir = g_clientPath;
        if (!g_hermesExePath.empty()) {
            size_t sep = g_hermesExePath.rfind(L'\\');
            if (sep != std::wstring::npos) hermesDir = g_hermesExePath.substr(0, sep);
        }
        CreateDirectoryW(hermesDir.c_str(), nullptr);

        // Kill HermesProxy if running — file lock would silently corrupt the update
        {
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        if (_wcsicmp(pe.szExeFile, L"HermesProxy.exe") == 0) {
                            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                            if (h) { TerminateProcess(h, 0); CloseHandle(h); }
                        }
                    } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }
            Sleep(500);
        }

        ok = ExtractZipSmart(tmpZip, hermesDir, true, [](int pct) {
            PostPct(70 + pct * 30 / 100);
        });
        DeleteFileW(tmpZip.c_str());
        if (ok) {
            // Update stored path in case this was a first-time install
            std::wstring newExe = hermesDir + L"\\HermesProxy.exe";
            if (GetFileAttributesW(newExe.c_str()) != INVALID_FILE_ATTRIBUTES)
                g_hermesExePath = newExe;
            PostPct(100);
        }
    } else {
        DeleteFileW(tmpZip.c_str());
    }
}

static void RunAddonUpdateCheck()
{
    if (IsDevBuild()) { PostText(L"Fetching from GitHub is disabled in dev mode to prevent reaching rate limits."); return; }
    std::wstring apiUrl = std::wstring(L"https://api.github.com/repos/")
        + ADDON_GH_OWNER + L"/" + ADDON_GH_REPO + L"/releases/latest";
    std::string json = HttpGet(apiUrl);
    if (json.empty()) return;

    std::string tag = JsonString(json, "tag_name");
    std::wstring remoteVer(tag.begin(), tag.end());
    std::wstring localVer = ReadAddonTocVersion();
    if (remoteVer.empty() || !IsNewer(localVer, remoteVer)) return;

    std::wstring payload = remoteVer + L"\n" + localVer;
    LRESULT r = SendMessageW(g_hwnd, WM_ASK_UPDATE, UC_ADDON,
        (LPARAM)(new std::wstring(payload)));
    if (r != IDYES) return;

    std::string assetUrl = FindAssetUrl(json);
    if (assetUrl.empty()) assetUrl = JsonString(json, "zipball_url");
    if (assetUrl.empty()) return;

    std::wstring assetW(assetUrl.begin(), assetUrl.end());
    std::wstring tmpZip = InstallTempFile(L"addon_update.zip");

    PostStatus(WS_DL_ADDON); PostPct(0);
    DlProgress dlAddon{L"Downloading WOW_HC addon...", 70};
    bool ok = HttpDownload(assetW, tmpZip,
        [&dlAddon](DWORD64 dl, DWORD64 tot) { dlAddon(dl, tot); });

    if (ok) {
        PostStatus(WS_EX_ADDON);
        std::wstring addonsDir = GetAddonsDir();
        // Ensure parent chain exists up to the addons directory
        std::wstring interfaceDir = addonsDir.substr(0, addonsDir.rfind(L'\\'));
        std::wstring gameDir      = interfaceDir.substr(0, interfaceDir.rfind(L'\\'));
        CreateDirectoryW(gameDir.c_str(), nullptr);
        CreateDirectoryW(interfaceDir.c_str(), nullptr);
        CreateDirectoryW(addonsDir.c_str(), nullptr);
        std::wstring addonDest = addonsDir + L"\\WOW_HC";
        DeleteDirRecursive(addonDest);
        CreateDirectoryW(addonDest.c_str(), nullptr);
        ok = ExtractZipSmart(tmpZip, addonDest, true, [](int pct) {
            PostPct(70 + pct * 30 / 100);
        });
        DeleteFileW(tmpZip.c_str());
        if (ok) { PostPct(100); }
    } else {
        DeleteFileW(tmpZip.c_str());
    }
}

// Checks for a newer launcher release, prompts, downloads and hot-swaps the EXE.
// Safe to call from any thread. Skips if a worker operation is in progress.
// forced=true: server-enforced minimum version — MB_OK only, no way to skip.
static void RunLauncherUpdateCheck(bool forced = false)
{
    if (IsDevBuild()) { PostText(L"Fetching from GitHub is disabled in dev mode to prevent reaching rate limits."); return; }
    if (g_workerBusy.load()) return;

    // Clean up any leftover .old from a previous self-update
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    DeleteFileW((std::wstring(exePath) + L".old").c_str());

    // /releases/latest skips pre-releases; /releases returns all (newest first)
    std::wstring apiUrl = std::wstring(L"https://api.github.com/repos/")
        + LAUNCHER_GH_OWNER + L"/" + LAUNCHER_GH_REPO
        + (g_testMode ? L"/releases" : L"/releases/latest");
    std::string json = HttpGet(apiUrl);
    if (json.empty()) return;

    std::string tag = JsonString(json, "tag_name");
    std::wstring remoteVer(tag.begin(), tag.end());
    std::wstring localVer = GetLauncherVersion();
    if (remoteVer.empty() || !IsNewer(localVer, remoteVer)) return;

    if (forced) {
        std::wstring text = L"Your launcher (v" + localVer + L") is too old to connect.\n\n"
            L"Version " + remoteVer + L" is required. Click OK to download and install it now.";
        MessageBoxW(g_hwnd, text.c_str(), L"Launcher Update Required", MB_OK | MB_ICONWARNING);
    } else {
        std::wstring payload = remoteVer + L"\n" + localVer;
        LRESULT r = SendMessageW(g_hwnd, WM_ASK_UPDATE, UC_LAUNCHER,
            (LPARAM)(new std::wstring(payload)));
        if (r != IDYES) return;
    }

    // Download the full zip (EXE + ui/ + DLLs) so the UI is updated too
    std::string zipUrl = FindFullZipAssetUrl(json);
    if (zipUrl.empty()) {
        zipUrl = std::string("https://github.com/") + "Novivy/wowhc-launcher"
               + "/releases/latest/download/" + LAUNCHER_FULL_ZIP_ASSET;
    }

    std::wstring zipUrlW(zipUrl.begin(), zipUrl.end());
    std::wstring tmpZip = TempFile(L"wowhc_launcher_update.zip");
    PostText(L"Downloading launcher update...");
    if (!HttpDownload(zipUrlW, tmpZip)) {
        DeleteFileW(tmpZip.c_str());
        MessageBoxW(g_hwnd, L"Failed to download the launcher update.\nCheck your internet connection and try again.", L"Update Failed", MB_OK | MB_ICONERROR);
        return;
    }

    // Extract new EXE to temp + overwrite ui/ in AppData — one PS script
    std::wstring tmpExe  = TempFile(L"wowhc_launcher_new.exe");
    auto escPS = [](const std::wstring& s) {
        std::wstring r;
        for (wchar_t c : s) { if (c == L'\'') r += L"''"; else r += c; }
        return r;
    };
    std::wstring script =
        L"Add-Type -AN System.IO.Compression.FileSystem\r\n"
        L"try {\r\n"
        L"  $z=[System.IO.Compression.ZipFile]::OpenRead('" + escPS(tmpZip) + L"')\r\n"
        L"  foreach($e in $z.Entries){\r\n"
        // New launcher EXE (root-level entry)
        L"    if($e.Name -eq 'WOW-HC-Launcher.exe' -and $e.FullName -notlike '*/*'){\r\n"
        L"      [System.IO.Compression.ZipFileExtensions]::ExtractToFile("
              L"$e,'" + escPS(tmpExe) + L"',$true)\r\n"
        L"    } elseif($e.FullName -like 'ui/*' -and $e.Name -ne ''){\r\n"
        // ui/ folder entries — extract into AppData so they survive EXE-only updates
        L"      $rel=$e.FullName -replace '/','\\'\r\n"
        L"      $dst='" + escPS(g_configDir) + L"\\'+$rel\r\n"
        L"      $dir=[System.IO.Path]::GetDirectoryName($dst)\r\n"
        L"      if(-not(Test-Path $dir)){New-Item -ItemType Directory -Force -Path $dir|Out-Null}\r\n"
        L"      [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e,$dst,$true)\r\n"
        L"    }\r\n"
        L"  }\r\n"
        L"  $z.Dispose()\r\n"
        L"} catch { [Console]::Error.WriteLine($_); exit 1 }\r\n";

    std::wstring scriptPath = TempFile(L"launcher_update_extract.ps1");
    bool psOk = false;
    if (WritePsScript(scriptPath, script)) {
        std::wstring cmd = L"powershell.exe -NonInteractive -ExecutionPolicy Bypass -File \""
            + scriptPath + L"\"";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
        STARTUPINFOW si = {}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 120000);
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            psOk = (code == 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        DeleteFileW(scriptPath.c_str());
    }
    DeleteFileW(tmpZip.c_str());

    if (!psOk || GetFileAttributesW(tmpExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(tmpExe.c_str());
        MessageBoxW(g_hwnd, L"Failed to extract the launcher update.\nThe previous version is still active.", L"Update Failed", MB_OK | MB_ICONERROR);
        return;
    }

    SendMessageW(g_hwnd, WM_APPLY_SELF_UPD, 0, (LPARAM)(new std::wstring(tmpExe)));
}

// ── FFmpeg DLL bootstrap ───────────────────────────────────────────────────────
// Called at startup: if FFmpeg DLLs are missing (new user / post-self-update),
// downloads WOW-HC-Launcher-Full.zip and extracts only the DLLs next to the EXE.
// The EXE starts fine without them because they are delay-loaded.

static std::wstring GetExeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.rfind(L'\\');
    return (pos != std::wstring::npos) ? p.substr(0, pos) : p;
}

static bool CopyUiFolder(const std::wstring& src, const std::wstring& dst)
{
    CreateDirectoryW(dst.c_str(), nullptr);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring s = src + L"\\" + fd.cFileName;
        std::wstring d = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ok &= CopyUiFolder(s, d);
        else
            ok &= (CopyFileW(s.c_str(), d.c_str(), FALSE) != 0);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

static bool FFmpegDllsPresent(const std::wstring& dir)
{
    return GetFileAttributesW((dir + L"\\avcodec-62.dll").c_str())
           != INVALID_FILE_ATTRIBUTES;
}

static void CheckAndBootstrapFFmpegDlls()
{
    AppendLog(L"FFmpeg bootstrap: clientPath='%s'", g_clientPath.c_str());
    if (g_clientPath.empty()) { AppendLog(L"FFmpeg bootstrap: clientPath empty, skipping"); return; }

    std::wstring dllDir = g_clientPath;

    RB_SetDllDir(dllDir); // set dir early so future LoadFFmpegDynamic() calls look in the right place

    if (FFmpegDllsPresent(dllDir)) {
        AppendLog(L"FFmpeg bootstrap: DLLs already present in '%s'", dllDir.c_str());
        return;
    }
    AppendLog(L"FFmpeg bootstrap: DLLs not present in '%s'", dllDir.c_str());

    if (IsDevBuild()) {
        AppendLog(L"FFmpeg bootstrap: dev build, skipping download");
        PostText(L"Fetching from GitHub is disabled in dev mode to prevent reaching rate limits.");
        return;
    }

    g_ffmpegBusy = true;
    PostText(L"Downloading FFmpeg libraries (first-time setup)...");
    PostPct(0);

    // Try version-specific release first (so pre-release tags work too), then
    // fall back to /releases/latest if the tag was deleted or the asset download
    // fails. Both the API 404 and a failed download trigger the next candidate.
    const char* verA = LAUNCHER_VERSION_STR;
    std::wstring verW(verA, verA + strlen(verA));
    std::wstring repoBase = std::wstring(L"https://api.github.com/repos/")
        + LAUNCHER_GH_OWNER + L"/" + LAUNCHER_GH_REPO;
    std::wstring apiUrls[] = {
        repoBase + L"/releases/tags/" + verW,
        repoBase + L"/releases/latest"
    };

    std::wstring tmpZip = TempFile(L"WOW-HC-Launcher-Full.zip");
    bool ok = false;
    for (const auto& apiUrl : apiUrls) {
        AppendLog(L"FFmpeg bootstrap: trying API %s", apiUrl.c_str());
        std::string json = HttpGet(apiUrl);
        if (json.empty()) { AppendLog(L"FFmpeg bootstrap: empty response (404 or network error)"); continue; }
        AppendLog(L"FFmpeg bootstrap: got API response (%zu bytes)", json.size());
        std::string url = FindFullZipAssetUrl(json);
        if (url.empty()) {
            AppendLog(L"FFmpeg bootstrap: no *full*.zip asset found in response");
            continue;
        }
        AppendLog(L"FFmpeg bootstrap: found zip asset: %hs", url.c_str());
        std::wstring zipUrl(url.begin(), url.end());
        ok = HttpDownload(zipUrl, tmpZip, [](DWORD64 dl, DWORD64 tot) {
            if (tot > 0) PostPct((int)(dl * 85 / tot));
        });
        AppendLog(L"FFmpeg bootstrap: download %s", ok ? L"succeeded" : L"failed");
        if (ok) break;
        DeleteFileW(tmpZip.c_str()); // discard partial file before retrying
    }
    if (!ok) {
        AppendLog(L"FFmpeg bootstrap: all attempts failed");
        DeleteFileW(tmpZip.c_str());
        g_ffmpegBusy = false;
        PostText(L"Failed to download FFmpeg libraries. Click Start Recording to retry.");
        PostPct(0);
        return;
    }

    PostText(L"Installing FFmpeg libraries...");
    PostPct(85);

    // Extract only .dll entries (skip the EXE in the ZIP) to the client folder.
    auto escPS = [](const std::wstring& s) {
        std::wstring r;
        for (wchar_t c : s) { if (c == L'\'') r += L"''"; else r += c; }
        return r;
    };
    std::wstring script =
        L"Add-Type -AN System.IO.Compression.FileSystem\r\n"
        L"try {\r\n"
        L"  $z=[System.IO.Compression.ZipFile]::OpenRead('" + escPS(tmpZip) + L"')\r\n"
        L"  foreach($e in $z.Entries){\r\n"
        L"    if($e.Name -like '*.dll'){\r\n"
        L"      [System.IO.Compression.ZipFileExtensions]::ExtractToFile("
        L"$e,'" + escPS(dllDir) + L"\\'+$e.Name,$true)\r\n"
        L"    }\r\n"
        L"  }\r\n"
        L"  $z.Dispose()\r\n"
        L"} catch { [Console]::Error.WriteLine($_); exit 1 }\r\n";

    std::wstring scriptPath = TempFile(L"launcher_ffmpeg_install.ps1");
    if (WritePsScript(scriptPath, script)) {
        std::wstring cmd = L"powershell.exe -NonInteractive -ExecutionPolicy Bypass -File \""
            + scriptPath + L"\"";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);

        STARTUPINFOW si = {}; si.cb = sizeof(si);
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 60000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        DeleteFileW(scriptPath.c_str());
    }

    DeleteFileW(tmpZip.c_str());
    PostPct(100);

    if (FFmpegDllsPresent(dllDir)) {
        AppendLog(L"FFmpeg bootstrap: installation succeeded");
        RB_SetDllDir(dllDir);
        PostText(L"FFmpeg libraries installed.");
    } else {
        AppendLog(L"FFmpeg bootstrap: DLLs still missing after extraction");
        PostText(L"FFmpeg install failed. Replay buffer will be unavailable.");
    }
    Sleep(1500);
    g_ffmpegBusy = false;
    PostPct(0);
}

// ── Exe search helpers ────────────────────────────────────────────────────────

static std::wstring FindExeInTree(const std::wstring& root, const wchar_t* exeName, int depth = 2)
{
    std::wstring direct = root + L"\\" + exeName;
    if (GetFileAttributesW(direct.c_str()) != INVALID_FILE_ATTRIBUTES) return direct;
    if (depth <= 0) return {};
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring result;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        result = FindExeInTree(root + L"\\" + fd.cFileName, exeName, depth - 1);
        if (!result.empty()) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return result;
}

static std::wstring FindExeNearby(const std::wstring& clientDir, const wchar_t* exeName)
{
    std::wstring r = FindExeInTree(clientDir, exeName, 2);
    if (!r.empty()) return r;
    size_t sep = clientDir.rfind(L'\\');
    if (sep != std::wstring::npos)
        r = FindExeInTree(clientDir.substr(0, sep), exeName, 1);
    return r;
}

// ── WoW installation detection ────────────────────────────────────────────────
struct WowInstallInfo {
    std::wstring wowExePath;
    std::wstring clientDir;
    ClientType   type;
};

static bool FindWowInstall(const std::wstring& folder, WowInstallInfo& info, int depth = 2)
{
    // ── 1.14.2: WowClassic.exe inside _classic_era_ ───────────────────────────
    // Case 1: folder IS _classic_era_ (WowClassic.exe directly inside)
    {
        std::wstring p = folder + L"\\WowClassic.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            info.wowExePath = p;
            info.type = CT_114;
            size_t sep = folder.rfind(L'\\');
            info.clientDir = (sep != std::wstring::npos) ? folder.substr(0, sep) : folder;
            return true;
        }
    }
    // Case 2: folder is client dir (contains _classic_era_)
    {
        std::wstring p = folder + L"\\_classic_era_\\WowClassic.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            info.wowExePath = p;
            info.clientDir  = folder;
            info.type = CT_114;
            return true;
        }
    }
    // Case 3: folder is launcher install root (contains client\_classic_era_)
    {
        std::wstring p = folder + L"\\client\\_classic_era_\\WowClassic.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            info.wowExePath = p;
            info.clientDir  = folder + L"\\client";
            info.type = CT_114;
            return true;
        }
    }
    // ── 1.12.1: Wow_tweaked.exe inside _classic_era_ (same layout as 1.14.2) ──
    // Case D: Wow_tweaked.exe directly in the selected folder.
    // clientDir = the folder itself so GetAddonsDir() can probe whether
    // _classic_era_ is a subfolder (launcher-installed) or not (flat install).
    {
        std::wstring p = folder + L"\\Wow_tweaked.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            info.wowExePath = p;
            info.clientDir  = folder;
            info.type = CT_112;
            return true;
        }
    }
    // Case E: folder is client dir (contains _classic_era_\Wow_tweaked.exe)
    {
        std::wstring p = folder + L"\\_classic_era_\\Wow_tweaked.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            info.wowExePath = p;
            info.clientDir  = folder;
            info.type = CT_112;
            return true;
        }
    }
    // Case F: folder is launcher install root (contains client\_classic_era_\Wow_tweaked.exe)
    {
        std::wstring p = folder + L"\\client\\_classic_era_\\Wow_tweaked.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            info.wowExePath = p;
            info.clientDir  = folder + L"\\client";
            info.type = CT_112;
            return true;
        }
    }
    // Case 4: scan immediate subdirectories so selecting a grandparent still works
    if (depth > 0) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((folder + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                if (FindWowInstall(folder + L"\\" + fd.cFileName, info, depth - 1)) {
                    FindClose(h);
                    return true;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    return false;
}

// Called by the 24-h timer while the launcher is open.
static void PeriodicUpdateCheck()
{
    RunLauncherUpdateCheck();
    if (!g_clientPath.empty()) {
        if (g_clientType == CT_114) {
            RunHermesUpdateCheck();
            // Refresh hermes path in case an update moved it
            if (g_hermesExePath.empty() || GetFileAttributesW(g_hermesExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::wstring f = FindExeInTree(g_clientPath, L"HermesProxy.exe", 2);
                if (!f.empty()) { g_hermesExePath = f; SaveConfig(); }
            }
        }
        RunAddonUpdateCheck(); // addon check runs for both versions
        WriteLastCheckTime();
    }
    if (g_playReady.load()) { PostStatus(WS_READY); PostPct(100); }
}

// ── Worker thread ──────────────────────────────────────────────────────────────
static void Worker()
{
    g_workerBusy = true;
    g_playReady  = false;

    // ── 1. Check client installation ──────────────────────────────────────────
    bool clientOk = false;
    if (!g_clientPath.empty()) {
        clientOk = GetFileAttributesW(ClientMarker().c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!clientOk && g_clientType == CT_114) {
            std::wstring arctium = g_clientPath + L"\\Arctium WoW Launcher.exe";
            if (GetFileAttributesW(arctium.c_str()) != INVALID_FILE_ATTRIBUTES)
                clientOk = true;
        }
        if (!clientOk && g_clientType == CT_114) {
            std::wstring wowExe = g_clientPath + L"\\_classic_era_\\WowClassic.exe";
            if (GetFileAttributesW(wowExe.c_str()) != INVALID_FILE_ATTRIBUTES)
                clientOk = true;
        }
        if (!clientOk && g_clientType == CT_112) {
            std::wstring def = g_clientPath + L"\\_classic_era_\\Wow_tweaked.exe";
            std::wstring probe = (!g_wowTweakedExePath.empty() &&
                GetFileAttributesW(g_wowTweakedExePath.c_str()) != INVALID_FILE_ATTRIBUTES)
                ? g_wowTweakedExePath : def;
            if (GetFileAttributesW(probe.c_str()) != INVALID_FILE_ATTRIBUTES) {
                clientOk = true;
                if (g_wowTweakedExePath != probe) g_wowTweakedExePath = probe;
            }
        }
        if (clientOk && GetFileAttributesW(ClientMarker().c_str()) == INVALID_FILE_ATTRIBUTES) {
            HANDLE h = CreateFileW(ClientMarker().c_str(), GENERIC_WRITE, 0,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
    }

    g_clientInstalled = clientOk;
    PostMessageW(g_hwnd, WM_SET_INSTALL_MODE, clientOk ? 1 : 0, 0);

    if (!clientOk) {
        PostStatus(WS_DL_CLIENT);
        PostPct(0);

        CreateDirectoryW(g_installPath.c_str(), nullptr);
        std::wstring tmpZip = InstallTempFile(L"wowclient_dl.zip");
        DlProgress dlClient{L"Downloading WoW client...", 65};
        const wchar_t* dlUrl = (g_clientType == CT_112) ? CLIENT_112_DOWNLOAD_URL : CLIENT_DOWNLOAD_URL;
        bool ok = HttpDownload(dlUrl, tmpZip,
            [&dlClient](DWORD64 dl, DWORD64 tot) { dlClient(dl, tot); });

        if (!ok) {
            DeleteFileW(tmpZip.c_str());
            PostStatus(WS_ERROR);
            g_workerBusy = false;
            PostMessageW(g_hwnd, WM_WORKER_DONE, 0, 0);
            return;
        }

        PostStatus(WS_EX_CLIENT);
        CreateDirectoryW(g_installPath.c_str(), nullptr);

        ok = ExtractZipSmart(tmpZip, g_installPath, true, [](int pct) {
            PostPct(65 + pct * 35 / 100);
        });
        DeleteFileW(tmpZip.c_str());

        if (!ok) {
            PostStatus(WS_ERROR);
            g_workerBusy = false;
            PostMessageW(g_hwnd, WM_WORKER_DONE, 0, 0);
            return;
        }

        CreateDirectoryW(g_clientPath.c_str(), nullptr);
        HANDLE hm = CreateFileW(ClientMarker().c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hm != INVALID_HANDLE_VALUE) CloseHandle(hm);

        // For 1.12.1, locate Wow_tweaked.exe (same structure: _classic_era_\Wow_tweaked.exe)
        if (g_clientType == CT_112) {
            std::wstring def = g_clientPath + L"\\_classic_era_\\Wow_tweaked.exe";
            if (GetFileAttributesW(def.c_str()) != INVALID_FILE_ATTRIBUTES)
                g_wowTweakedExePath = def;
            else {
                std::wstring found = FindExeInTree(g_clientPath, L"Wow_tweaked.exe", 3);
                if (!found.empty()) g_wowTweakedExePath = found;
            }
        }

        g_clientInstalled = true;
        g_freshInstall    = true;
        PostMessageW(g_hwnd, WM_SET_INSTALL_MODE, 1, 0);
        PostPct(100);
    }

    // ── 1b. Ensure FFmpeg DLLs are present in the client folder ──────────────────
    CheckAndBootstrapFFmpegDlls();

    // ── 2+3. Check HermesProxy and addon updates (1.14.2 only) ─────────────────
    PostStatus(WS_CHECKING);
    PostPct(0);
    if (g_clientType == CT_114) {
        RunHermesUpdateCheck();
        // Refresh Arctium/Hermes paths for new installs if not yet found
        if (g_arctiumExePath.empty() || GetFileAttributesW(g_arctiumExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wstring found = FindExeInTree(g_clientPath, L"Arctium WoW Launcher.exe", 2);
            if (!found.empty()) g_arctiumExePath = found;
        }
        if (g_hermesExePath.empty() || GetFileAttributesW(g_hermesExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wstring found = FindExeInTree(g_clientPath, L"HermesProxy.exe", 2);
            if (!found.empty()) g_hermesExePath = found;
        }
    }
    RunAddonUpdateCheck(); // addon is compatible with both 1.14.2 and 1.12.1
    WriteLastCheckTime();
    SaveConfig();

    // ── 4. Verify all required executables are present ───────────────────────────
    g_workerBusy = false;

    if (g_clientType == CT_112) {
        // 1.12.1: only need Wow_tweaked.exe (lives in _classic_era_ same as WowClassic.exe)
        if (g_wowTweakedExePath.empty() || GetFileAttributesW(g_wowTweakedExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wstring def = g_clientPath + L"\\_classic_era_\\Wow_tweaked.exe";
            if (GetFileAttributesW(def.c_str()) != INVALID_FILE_ATTRIBUTES)
                g_wowTweakedExePath = def;
            else {
                std::wstring found = FindExeInTree(g_clientPath, L"Wow_tweaked.exe", 3);
                if (!found.empty()) g_wowTweakedExePath = found;
            }
            if (!g_wowTweakedExePath.empty()) SaveConfig();
        }
        bool wowOk = !g_wowTweakedExePath.empty() &&
            GetFileAttributesW(g_wowTweakedExePath.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (wowOk) {
            PostStatus(WS_READY);
            PostPct(100);
        } else {
            PostText(L"Missing: Wow_tweaked.exe");
            PostStatus(WS_ERROR);
        }
        PostMessageW(g_hwnd, WM_WORKER_DONE, wowOk ? 1 : 0, 0);
    } else {
        // 1.14.2: need HermesProxy + Arctium + addon
        bool hermesOk  = !g_hermesExePath.empty()  &&
            GetFileAttributesW(g_hermesExePath.c_str())  != INVALID_FILE_ATTRIBUTES;
        bool arctiumOk = !g_arctiumExePath.empty() &&
            GetFileAttributesW(g_arctiumExePath.c_str()) != INVALID_FILE_ATTRIBUTES;
        bool addonOk   = GetFileAttributesW(
            (GetAddonsDir() + L"\\WOW_HC").c_str()) != INVALID_FILE_ATTRIBUTES;

        bool allReady = hermesOk && arctiumOk && addonOk;

        if (allReady) {
            PostStatus(WS_READY);
            PostPct(100);
        } else {
            std::wstring missing;
            if (!hermesOk)  missing += L"HermesProxy.exe  ";
            if (!arctiumOk) missing += L"Arctium WoW Launcher.exe  ";
            if (!addonOk)   missing += L"WOW_HC addon";
            PostText(L"Missing components: " + missing);
            PostStatus(WS_ERROR);
        }
        PostMessageW(g_hwnd, WM_WORKER_DONE, allReady ? 1 : 0, 0);
    }
}

// ── Transfer worker ────────────────────────────────────────────────────────────

// Returns the directory containing HermesProxy.exe in the old install, or empty.
// Search order: same level as _classic_era_, grandparent, then subdirs of parent.
static std::wstring FindHermesDirInOldInstall(const std::wstring& srcClassicEra)
{
    std::wstring parent = srcClassicEra.substr(0, srcClassicEra.rfind(L'\\'));

    if (GetFileAttributesW((parent + L"\\HermesProxy.exe").c_str()) != INVALID_FILE_ATTRIBUTES)
        return parent;

    size_t gp = parent.rfind(L'\\');
    if (gp != std::wstring::npos) {
        std::wstring grandParent = parent.substr(0, gp);
        if (GetFileAttributesW((grandParent + L"\\HermesProxy.exe").c_str()) != INVALID_FILE_ATTRIBUTES)
            return grandParent;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((parent + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring child = parent + L"\\" + fd.cFileName;
                if (GetFileAttributesW((child + L"\\HermesProxy.exe").c_str()) != INVALID_FILE_ATTRIBUTES) {
                    FindClose(h);
                    return child;
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    return {};
}

// srcClassicEra is the selected _classic_era_ folder from the old installation.
static void TransferWorker(std::wstring srcClassicEra)
{
    g_workerBusy = true;

    std::wstring dstBase    = g_clientPath + L"\\_classic_era_";
    std::wstring srcWtf     = srcClassicEra + L"\\WTF";
    std::wstring srcAddons  = srcClassicEra + L"\\Interface\\AddOns";
    std::wstring dstWtf     = dstBase + L"\\WTF";
    std::wstring dstAddons  = dstBase + L"\\Interface\\AddOns";

    // Locate AccountData next to HermesProxy.exe in the old install.
    // In the new install HermesProxy.exe is at {clientPath}\, so AccountData goes there too.
    std::wstring oldHermesDir   = FindHermesDirInOldInstall(srcClassicEra);
    std::wstring srcAccountData = oldHermesDir.empty() ? L"" : (oldHermesDir + L"\\AccountData");
    std::wstring dstAccountData = g_clientPath + L"\\AccountData";
    bool hasAccountData = !srcAccountData.empty() &&
        GetFileAttributesW(srcAccountData.c_str()) != INVALID_FILE_ATTRIBUTES;

    PostStatus(WS_TRANSFER);
    PostPct(0);
    PostText(L"Counting files...");

    int total = CountFiles(srcWtf) + CountFiles(srcAddons);
    if (hasAccountData) total += CountFiles(srcAccountData);
    if (total < 1) total = 1;

    PostText(L"Clearing old data from new installation...");
    DeleteDirRecursive(dstBase + L"\\Cache");
    DeleteDirRecursive(dstWtf);
    DeleteDirRecursive(dstAddons);
    if (hasAccountData) DeleteDirRecursive(dstAccountData);
    PostPct(5);

    int copied = 0;
    bool ok = true;

    PostText(L"Copying WTF (settings & macros)...");
    ok = CopyDirRecursive(srcWtf, dstWtf, copied, total, [](int pct) {
        PostPct(5 + pct * 90 / 100);
    });

    if (ok) {
        PostText(L"Copying Interface/AddOns...");
        ok = CopyDirRecursive(srcAddons, dstAddons, copied, total, [](int pct) {
            PostPct(5 + pct * 90 / 100);
        });
    }

    if (ok && hasAccountData) {
        PostText(L"Copying AccountData...");
        ok = CopyDirRecursive(srcAccountData, dstAccountData, copied, total, [](int pct) {
            PostPct(5 + pct * 90 / 100);
        });
    }

    if (ok) {
        PostPct(100);
        PostText(L"Transfer complete! UI, macros, addons and account data have been copied.");
    } else {
        PostText(L"Transfer failed. Some files may not have been copied.");
    }

    g_workerBusy = false;
    PostMessageW(g_hwnd, WM_TRANSFER_DONE, ok ? 1 : 0, 0);
}

// ── Button hover subclass ───────────────────────────────────────────────────────
static LRESULT CALLBACK BtnSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    bool* pHover = reinterpret_cast<bool*>(dwRefData);
    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    } else if (msg == WM_MOUSEMOVE && !*pHover) {
        *pHover = true;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        SetTimer(GetParent(hwnd), ID_TIMER_HOVER, 16, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (msg == WM_MOUSELEAVE) {
        *pHover = false;
        SetTimer(GetParent(hwnd), ID_TIMER_HOVER, 16, nullptr);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void EnsureDesktopShortcut(bool onlyIfAlreadyExists = false);

// ── Shared color helpers ───────────────────────────────────────────────────────
static COLORREF LerpColor(COLORREF a, COLORREF b, float t) {
    return RGB(
        (int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
        (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
        (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

// ── Modal button helpers ───────────────────────────────────────────────────────
// Simple hover subclass for modal buttons (no animation — immediate state change).
static LRESULT CALLBACK ModalBtnSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                              UINT_PTR /*id*/, DWORD_PTR data)
{
    bool* pHover = reinterpret_cast<bool*>(data);
    if (msg == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
    if (msg == WM_MOUSEMOVE && !*pHover) {
        *pHover = true;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (msg == WM_MOUSELEAVE) {
        *pHover = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// Draws a modal dialog button matching the main app's dark style.
// isSecondary = Open-button style (darker); primary = Browse/Record style.
static void DrawModalButton(DRAWITEMSTRUCT* dis, bool isSecondary, bool hovered)
{
    RECT rc      = dis->rcItem;
    HDC  hdc     = dis->hDC;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    float t      = hovered ? 1.0f : 0.0f;

    COLORREF bg;
    if      (pressed && isSecondary) bg = RGB(10,  8,  6);
    else if (pressed)                bg = RGB(20, 16, 10);
    else if (isSecondary)            bg = LerpColor(RGB(20, 16, 10), RGB(29, 24, 16), t);
    else                             bg = LerpColor(RGB(29, 24, 16), RGB(40, 30, 16), t);

    HBRUSH hbr = CreateSolidBrush(bg);
    FillRect(hdc, &rc, hbr);
    DeleteObject(hbr);

    COLORREF borderClr = isSecondary
        ? (pressed ? RGB(46, 34, 18) : LerpColor(RGB(30, 22, 12), RGB(46, 34, 18), t))
        : (pressed ? RGB(46, 34, 18) : LerpColor(RGB(46, 34, 18), RGB(90, 68, 24), t));
    HPEN hpen    = CreatePen(PS_SOLID, 1, borderClr);
    HPEN hpenOld = (HPEN)SelectObject(hdc, hpen);
    MoveToEx(hdc, rc.left,    rc.top,      nullptr);
    LineTo  (hdc, rc.right-1, rc.top);
    LineTo  (hdc, rc.right-1, rc.bottom-1);
    LineTo  (hdc, rc.left,    rc.bottom-1);
    LineTo  (hdc, rc.left,    rc.top);
    SelectObject(hdc, hpenOld);
    DeleteObject(hpen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT);
    HFONT old = (HFONT)SelectObject(hdc, DlgFont());
    wchar_t txt[128] = {};
    GetWindowTextW(dis->hwndItem, txt, 128);
    DrawTextW(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, old);

    if (dis->itemState & ODS_FOCUS)
        DrawFocusRect(hdc, &rc);
}

// ── Realm config update ────────────────────────────────────────────────────────
static void UpdateRealmConfig(int realmIndex)
{
    const wchar_t* server = (realmIndex == 1) ? REALM_PTR_SERVER : REALM_NORMAL_SERVER;
    g_realmIndex = realmIndex;
    wchar_t riBuf[8]; swprintf_s(riBuf, L"%d", realmIndex);
    WritePrivateProfileStringW(L"Launcher", L"RealmIndex", riBuf, ConfigPath().c_str());

    if (g_clientType == CT_114 && !g_hermesExePath.empty()) {
        size_t sep = g_hermesExePath.rfind(L'\\');
        std::wstring dir = (sep != std::wstring::npos) ? g_hermesExePath.substr(0, sep) : L"";
        if (dir.empty()) return;
        std::wstring cfgPath = dir + L"\\HermesProxy.config";

        HANDLE hf = CreateFileW(cfgPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return;
        DWORD sz = GetFileSize(hf, nullptr);
        std::string content(sz, '\0');
        DWORD rd = 0;
        ReadFile(hf, content.data(), sz, &rd, nullptr);
        CloseHandle(hf);
        content.resize(rd);

        size_t pos = content.find("key=\"ServerAddress\"");
        if (pos == std::string::npos) return;
        size_t vp = content.find("value=\"", pos);
        if (vp == std::string::npos) return;
        vp += 7;
        size_t ve = content.find('"', vp);
        if (ve == std::string::npos) return;

        std::string serverA;
        for (const wchar_t* p = server; *p; ++p) serverA += (char)*p;
        content = content.substr(0, vp) + serverA + content.substr(ve);

        HANDLE hfw = CreateFileW(cfgPath.c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hfw == INVALID_HANDLE_VALUE) return;
        DWORD wr = 0;
        WriteFile(hfw, content.data(), (DWORD)content.size(), &wr, nullptr);
        CloseHandle(hfw);

    } else if (g_clientType == CT_112 && !g_wowTweakedExePath.empty()) {
        size_t sep = g_wowTweakedExePath.rfind(L'\\');
        std::wstring dir = (sep != std::wstring::npos) ? g_wowTweakedExePath.substr(0, sep) : L"";
        if (dir.empty()) return;
        std::wstring wtfPath = dir + L"\\realmlist.wtf";

        std::string serverA;
        for (const wchar_t* p = server; *p; ++p) serverA += (char)*p;
        std::string content = "set realmlist " + serverA + "\r\n";

        HANDLE hf = CreateFileW(wtfPath.c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return;
        DWORD wr = 0;
        WriteFile(hf, content.data(), (DWORD)content.size(), &wr, nullptr);
        CloseHandle(hf);
    }
}

// ── PTR info dialog ────────────────────────────────────────────────────────────
struct PTRDlgState { bool done; bool hoverReq; bool hoverDismiss; };

static LRESULT CALLBACK PTRDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        auto D = [hwnd](int px) { return MulDiv(px, GetDpiForWindow(hwnd), 96); };
        auto SF = [](HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); };

        PTRDlgState* st = (PTRDlgState*)((LPCREATESTRUCT)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

        HWND hDesc = CreateWindowExW(0, L"STATIC",
            L"The Player Testing Realm (PTR) requires special access.\r\n"
            L"Click \"Request Access\" to apply for PTR access on the WOW-HC website.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            D(14), D(14), D(312), D(56), hwnd,
            nullptr, nullptr, nullptr);
        SF(hDesc, DlgFont());

        HWND hReq = CreateWindowExW(0, L"BUTTON", L"Request Access",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            D(14), D(84), D(130), D(26), hwnd, (HMENU)2001, nullptr, nullptr);
        SF(hReq, DlgFont());
        SetWindowSubclass(hReq, ModalBtnSubclassProc, 0, (DWORD_PTR)&st->hoverReq);

        HWND hDis = CreateWindowExW(0, L"BUTTON", L"Dismiss",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            D(156), D(84), D(86), D(26), hwnd, (HMENU)IDCANCEL, nullptr, nullptr);
        SF(hDis, DlgFont());
        SetWindowSubclass(hDis, ModalBtnSubclassProc, 1, (DWORD_PTR)&st->hoverDismiss);
        break;
    }
    case WM_COMMAND: {
        PTRDlgState* st = (PTRDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        WORD id = LOWORD(wp);
        if (id == 2001) {
            ShellExecuteW(nullptr, L"open", L"https://wow-hc.com/ptr",
                nullptr, nullptr, SW_SHOWNORMAL);
        } else if (id == IDCANCEL) {
            st->done = true;
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        PTRDlgState* st = (PTRDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (st) st->done = true;
        PostMessageW(g_hwnd, WM_NULL, 0, 0);
        break;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_hbrBg);
        return 1;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        auto* st  = (PTRDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        bool isSecondary = (dis->CtlID == IDCANCEL);
        bool hov = isSecondary ? st->hoverDismiss : st->hoverReq;
        DrawModalButton(dis, isSecondary, hov);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, CLR_BG);
        SetTextColor(hdc, CLR_TEXT);
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)g_hbrBg;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowPTRDialog(HWND hParent)
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = PTRDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = g_hbrBg;
        wc.lpszClassName = L"WOWHCPTRDlg";
        RegisterClassExW(&wc);
        registered = true;
    }

    UINT dpi = GetDpiForWindow(hParent ? hParent : GetDesktopWindow());
    // Size from desired client area so title bar + borders scale correctly at any DPI
    RECT rcAdj = { 0, 0, MulDiv(340, dpi, 96), MulDiv(124, dpi, 96) };
    AdjustWindowRectExForDpi(&rcAdj, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             FALSE, WS_EX_DLGMODALFRAME, dpi);
    int w = rcAdj.right - rcAdj.left;
    int h = rcAdj.bottom - rcAdj.top;
    RECT pr; GetWindowRect(hParent, &pr);
    int x = pr.left + (pr.right - pr.left - w) / 2;
    int y = pr.top  + (pr.bottom - pr.top  - h) / 2;

    PTRDlgState state = {};
    EnableWindow(hParent, FALSE);

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"WOWHCPTRDlg",
        L"Player Testing Realm (PTR)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, hParent, nullptr, GetModuleHandleW(nullptr), &state);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG m;
    while (!state.done && GetMessageW(&m, nullptr, 0, 0)) {
        if (IsWindow(hwnd) && IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m); DispatchMessageW(&m);
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
}

// ── Version picker dialog ──────────────────────────────────────────────────────
struct VPState { ClientType result; bool done; bool hoverOk; bool hoverCancel; };

static LRESULT CALLBACK VersionPickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        auto D = [hwnd](int px) { return MulDiv(px, GetDpiForWindow(hwnd), 96); };
        auto SF = [](HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); };

        HWND hDesc = CreateWindowExW(0, L"STATIC",
            L"Choose which WoW client version to install:",
            WS_CHILD|WS_VISIBLE, D(14), D(14), D(366), D(18), hwnd,
            nullptr, nullptr, nullptr);
        SF(hDesc, DlgFont());

        HWND hR1 = CreateWindowExW(0, L"BUTTON",
            L"Modern 1.14.2",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTORADIOBUTTON|WS_GROUP,
            D(14), D(54), D(366), D(20), hwnd, (HMENU)1001, nullptr, nullptr);
        SF(hR1, DlgFont());
        SendMessageW(hR1, BM_SETCHECK, BST_CHECKED, 0);

        HWND hSub1 = CreateWindowExW(0, L"STATIC",
            L"(recommended)",
            WS_CHILD|WS_VISIBLE, D(32), D(76), D(352), D(16), hwnd,
            nullptr, nullptr, nullptr);
        SF(hSub1, DlgSmallFont());

        HWND hR2 = CreateWindowExW(0, L"BUTTON",
            L"Vanilla 1.12.1",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTORADIOBUTTON,
            D(14), D(104), D(366), D(20), hwnd, (HMENU)1002, nullptr, nullptr);
        SF(hR2, DlgFont());

        HWND hSub2 = CreateWindowExW(0, L"STATIC",
            L"",
            WS_CHILD|WS_VISIBLE, D(32), D(126), D(352), D(16), hwnd,
            nullptr, nullptr, nullptr);
        SF(hSub2, DlgSmallFont());

        VPState* st = (VPState*)((LPCREATESTRUCT)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

        HWND hBack = CreateWindowExW(0, L"BUTTON", L"Back",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            D(107), D(186), D(86), D(26), hwnd, (HMENU)IDCANCEL, nullptr, nullptr);
        SF(hBack, DlgFont());
        SetWindowSubclass(hBack, ModalBtnSubclassProc, 0, (DWORD_PTR)&st->hoverCancel);

        HWND hOk = CreateWindowExW(0, L"BUTTON", L"Continue",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            D(201), D(186), D(86), D(26), hwnd, (HMENU)IDOK, nullptr, nullptr);
        SF(hOk, DlgFont());
        SetWindowSubclass(hOk, ModalBtnSubclassProc, 1, (DWORD_PTR)&st->hoverOk);
        break;
    }
    case WM_COMMAND: {
        VPState* st = (VPState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        WORD id = LOWORD(wp);
        if (id == IDOK) {
            HWND hR1 = GetDlgItem(hwnd, 1001);
            st->result = (SendMessageW(hR1, BM_GETCHECK, 0, 0) == BST_CHECKED) ? CT_114 : CT_112;
            st->done = true;
            DestroyWindow(hwnd);
        } else if (id == IDCANCEL) {
            st->result = CT_UNKNOWN;
            st->done = true;
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_DESTROY: {
        VPState* st = (VPState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (st) st->done = true;
        PostMessageW(g_hwnd, WM_NULL, 0, 0);
        break;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        auto* st  = (VPState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        bool isSecondary = (dis->CtlID == IDCANCEL);
        bool hov = isSecondary ? st->hoverCancel : st->hoverOk;
        DrawModalButton(dis, isSecondary, hov);
        return TRUE;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_hbrBg);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, CLR_BG);
        SetTextColor(hdc, CLR_TEXT);
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)g_hbrBg;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static ClientType ShowVersionPickerDialog(HWND hParent)
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = VersionPickerProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = g_hbrBg;
        wc.lpszClassName = L"WOWHCVersionPicker";
        RegisterClassExW(&wc);
        registered = true;
    }

    UINT dpi = GetDpiForWindow(hParent ? hParent : GetDesktopWindow());
    int w = MulDiv(394, dpi, 96);
    int h = MulDiv(270, dpi, 96);
    RECT pr; GetWindowRect(hParent, &pr);
    int x = pr.left + (pr.right - pr.left - w) / 2;
    int y = pr.top  + (pr.bottom - pr.top - h) / 2;

    VPState state = {};
    EnableWindow(hParent, FALSE);

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"WOWHCVersionPicker",
        L"Choose WoW Client Version",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, hParent, nullptr, GetModuleHandleW(nullptr), &state);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG m;
    while (!state.done && GetMessageW(&m, nullptr, 0, 0)) {
        if (IsWindow(hwnd) && IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m); DispatchMessageW(&m);
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
    return state.result;
}

// ── Install mode picker (New Install vs Existing Install) ─────────────────────
// Returns true = new install, false = existing install, or -1 (cast to bool via
// a tri-state int) via the VPState pattern. We reuse VPState: result CT_114 =
// "new install", CT_112 = "existing install", CT_UNKNOWN = cancelled.

static LRESULT CALLBACK InstallModeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        auto D = [hwnd](int px) { return MulDiv(px, GetDpiForWindow(hwnd), 96); };
        auto SF = [](HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); };

        HWND hDesc = CreateWindowExW(0, L"STATIC",
            L"How would you like to get started?",
            WS_CHILD|WS_VISIBLE, D(14), D(14), D(366), D(18), hwnd,
            nullptr, nullptr, nullptr);
        SF(hDesc, DlgFont());

        HWND hR1 = CreateWindowExW(0, L"BUTTON",
            L"New installation  -  Download and Install a fresh client",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTORADIOBUTTON|WS_GROUP,
            D(14), D(54), D(366), D(20), hwnd, (HMENU)2001, nullptr, nullptr);
        SF(hR1, DlgFont());
        SendMessageW(hR1, BM_SETCHECK, BST_CHECKED, 0); // default

        HWND hSub1 = CreateWindowExW(0, L"STATIC",
            L"Choose your preferred version on the next screen.",
            WS_CHILD|WS_VISIBLE, D(32), D(76), D(352), D(16), hwnd,
            nullptr, nullptr, nullptr);
        SF(hSub1, DlgSmallFont());

        HWND hR2 = CreateWindowExW(0, L"BUTTON",
            L"Existing installation  -  Point to an existing folder",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTORADIOBUTTON,
            D(14), D(120), D(366), D(20), hwnd, (HMENU)2002, nullptr, nullptr);
        SF(hR2, DlgFont());

        HWND hSub2 = CreateWindowExW(0, L"STATIC",
            L"Version is auto-detected from the selected folder.",
            WS_CHILD|WS_VISIBLE, D(32), D(142), D(352), D(16), hwnd,
            nullptr, nullptr, nullptr);
        SF(hSub2, DlgSmallFont());

        VPState* st = (VPState*)((LPCREATESTRUCT)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

        HWND hOk = CreateWindowExW(0, L"BUTTON", L"Continue",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            D(154), D(186), D(86), D(26), hwnd, (HMENU)IDOK, nullptr, nullptr);
        SF(hOk, DlgFont());
        SetWindowSubclass(hOk, ModalBtnSubclassProc, 0, (DWORD_PTR)&st->hoverOk);
        break;
    }
    case WM_COMMAND: {
        VPState* st = (VPState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        WORD id = LOWORD(wp);
        if (id == IDOK) {
            HWND hR1 = GetDlgItem(hwnd, 2001);
            st->result = (SendMessageW(hR1, BM_GETCHECK, 0, 0) == BST_CHECKED) ? CT_114 : CT_112;
            st->done = true;
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_DESTROY: {
        VPState* st = (VPState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (st) st->done = true;
        PostMessageW(g_hwnd, WM_NULL, 0, 0);
        break;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        auto* st  = (VPState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        DrawModalButton(dis, false, st->hoverOk);
        return TRUE;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_hbrBg);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        SetBkMode((HDC)wp, TRANSPARENT);
        SetBkColor((HDC)wp, CLR_BG);
        SetTextColor((HDC)wp, CLR_TEXT);
        return (LRESULT)g_hbrBg;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Returns CT_114 = "new install", CT_112 = "existing install", CT_UNKNOWN = cancelled
static ClientType ShowInstallModeDialog(HWND hParent)
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = InstallModeProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = g_hbrBg;
        wc.lpszClassName = L"WOWHCInstallMode";
        RegisterClassExW(&wc);
        registered = true;
    }

    UINT dpi = GetDpiForWindow(hParent ? hParent : GetDesktopWindow());
    int w = MulDiv(394, dpi, 96);
    int h = MulDiv(270, dpi, 96);
    RECT pr; GetWindowRect(hParent, &pr);
    int x = pr.left + (pr.right - pr.left - w) / 2;
    int y = pr.top  + (pr.bottom - pr.top - h) / 2;

    VPState state = {};
    EnableWindow(hParent, FALSE);

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"WOWHCInstallMode",
        L"Get Started",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, hParent, nullptr, GetModuleHandleW(nullptr), &state);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG m;
    while (!state.done && GetMessageW(&m, nullptr, 0, 0)) {
        if (IsWindow(hwnd) && IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m); DispatchMessageW(&m);
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
    return state.result;
}

// Small red circle on a transparent background, used as a taskbar overlay badge.
static HICON CreateRecordingOverlayIcon(int sz)
{
    Gdiplus::Bitmap canvas(sz, sz, PixelFormat32bppARGB);
    Gdiplus::BitmapData bd;
    Gdiplus::Rect fr(0, 0, sz, sz);
    canvas.LockBits(&fr, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd);
    auto* px = static_cast<DWORD*>(bd.Scan0);
    float cx = sz * 0.65f, cy = sz * 0.75f, r = sz * 0.315f;
    for (int y = 0; y < sz; y++)
        for (int x = 0; x < sz; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float a = std::max(0.0f, std::min(1.0f, r - sqrtf(dx*dx + dy*dy) + 0.5f));
            DWORD av = (DWORD)(a * 255.0f + 0.5f);
            px[y * sz + x] = (av << 24) | 0x00DD2010;
        }
    canvas.UnlockBits(&bd);
    HICON hIcon = nullptr;
    canvas.GetHICON(&hIcon);
    return hIcon;
}

// ── WebView2 message dispatcher ────────────────────────────────────────────────
static void HandleWebMessage(HWND hwnd, const std::string& j)
{
    std::string action = JsonString(j, "action");

    if (action == "ready") {
        g_wvPageReady = true;
        PostStateToWebView();
        std::thread(FetchLiveData).detach();
        if (IsDevBuild() || g_testMode) g_webview->OpenDevToolsWindow();
    }
    else if (action == "installModeChoice") {
        std::string choice = JsonString(j, "choice");
        g_reactModalResult = (choice == "existing") ? 2 : 1;
        g_reactModalDone   = true;
    }
    else if (action == "installModeClose") {
        g_reactModalResult = 0;
        g_reactModalDone   = true;
    }
    else if (action == "versionPickerChoice") {
        int ver = JsonInt(j, "version");
        g_reactModalResult = (ver == 112) ? 2 : 1;
        g_reactModalDone   = true;
    }
    else if (action == "versionPickerBack") {
        g_reactModalResult = 0;
        g_reactModalDone   = true;
    }
    else if (action == "ptrRequestAccess") {
        ShellExecuteW(nullptr, L"open", L"https://wow-hc.com/ptr", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (action == "ptrDismiss") {
        // JS already dismissed — nothing for C++ to do
    }
    else if (action == "startGame") {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_PLAY, BN_CLICKED), 0);
    }
    else if (action == "browse") {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_BROWSE, BN_CLICKED), 0);
    }
    else if (action == "openFolder") {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_OPEN, BN_CLICKED), 0);
    }
    else if (action == "startRecording" || action == "stopRecording") {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_RECORD, BN_CLICKED), 0);
    }
    else if (action == "saveReplay") {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_SAVE_REPLAY, BN_CLICKED), 0);
    }
    else if (action == "openRecordSettings") {
        PostMessageW(hwnd, WM_REC_SETTINGS_OPEN, 0, 0);
    }
    else if (action == "recordSettingsBrowse") {
        PostMessageW(hwnd, WM_REC_SETTINGS_BROWSE, 0, 0);
    }
    else if (action == "recordSettingsStartStop") {
        auto* ps = new std::string(j);
        PostMessageW(hwnd, WM_REC_SETTINGS_TOGGLE, 0, (LPARAM)ps);
    }
    else if (action == "recordSettingsSaveNow") {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_SAVE_REPLAY, BN_CLICKED), 0);
    }
    else if (action == "recordSettingsClose") {
        auto* ps = new std::string(j);
        PostMessageW(hwnd, WM_REC_SETTINGS_CLOSE, 0, (LPARAM)ps);
    }
    else if (action == "uploadReplays") {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_UPLOAD, BN_CLICKED), 0);
    }
    else if (action == "openSettings") {
        PostShowModal(L"generalSettings");
        PostGeneralSettingsStateToWebView();
    }
    else if (action == "generalSettingsClose") {
        auto* ps = new std::string(j);
        PostMessageW(hwnd, WM_GEN_SETTINGS_CLOSE, 0, (LPARAM)ps);
    }
    else if (action == "setRealm") {
        int idx = JsonInt(j, "index");
        PostMessageW(hwnd, WM_SET_REALM, (WPARAM)idx, 0);
    }
    else if (action == "startDrag") {
        // Let Windows move the borderless window as if the user dragged the caption
        ReleaseCapture();
        POINT pt; GetCursorPos(&pt);
        PostMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
    }
    else if (action == "minimize") {
        ShowWindow(hwnd, SW_MINIMIZE);
    }
    else if (action == "close") {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    else if (action == "openGetHelp") {
        ShellExecuteW(nullptr, L"open", L"https://wow-hc.com/support/appeal", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (action == "openGetAddons") {
        ShellExecuteW(nullptr, L"open", L"https://wow-hc.com/addons/classic", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (action == "openAddonsFolder") {
        if (!g_clientPath.empty()) {
            std::wstring addonsPath = g_clientPath + L"\\_classic_era_\\Interface\\AddOns";
            ShellExecuteW(nullptr, L"open", addonsPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
    else if (action == "checkForUpdates") {
        if (!g_workerBusy) {
            std::thread([]() { PeriodicUpdateCheck(); }).detach();
        }
    }
    else if (action == "openWebsite") {
        ShellExecuteW(nullptr, L"open", L"https://wow-hc.com", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (action == "openUrl") {
        std::string url = JsonString(j, "url");
        if (!url.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
            std::wstring wurl(wlen > 1 ? wlen - 1 : 0, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl.data(), wlen);
            ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}

// ── Window procedure ───────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_taskbarCreatedMsg && msg == g_taskbarCreatedMsg) {
        if (g_pTaskbar) { g_pTaskbar->Release(); g_pTaskbar = nullptr; }
        if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pTaskbar))))
            g_pTaskbar->HrInit();
        // Restore progress that arrived before the taskbar was ready
        if (g_pTaskbar && g_taskbarHasProgress) {
            g_pTaskbar->SetProgressState(g_hwnd, TBPF_NORMAL);
            g_pTaskbar->SetProgressValue(g_hwnd, g_taskbarLastPct, 100);
        }
        // Restore recording overlay badge (e.g. after Explorer restart)
        if (RB_IsRunning()) {
            if (!g_hIconRecordingOverlay) g_hIconRecordingOverlay = CreateRecordingOverlayIcon(GetSystemMetrics(SM_CXSMICON));
            g_pTaskbar->SetOverlayIcon(g_hwnd, g_hIconRecordingOverlay, L"Recording");
        }
        return 0;
    }

    switch (msg)
    {
    case WM_CREATE:
    {
        // Force NC recalculation so WM_NCCALCSIZE suppresses the caption before
        // the window is first painted (avoids the caption flash on launch).
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        SetTimer(hwnd, ID_TIMER_UPDATE, 24u * 60u * 60u * 1000u, nullptr);
        SetTimer(hwnd, ID_TIMER_LIVE,   10u * 60u * 1000u, nullptr);

        g_currentStatus = STATUS_TEXT[WS_NO_PATH];

        // Start WebView2 (async — UI ready callback posts WM_STARTUP_CHECK_DONE)
        InitWebView2(hwnd);

        // Start launcher update + FFmpeg check in parallel with WebView2 init
        std::thread([]() {
            RunLauncherUpdateCheck();
            CheckAndBootstrapFFmpegDlls();
            PostMessageW(g_hwnd, WM_STARTUP_CHECK_DONE, 0, 0);
        }).detach();

        break;
    }

    case WM_SIZE:
    {
        if (g_wvCtrl) {
            RECT rc; GetClientRect(hwnd, &rc);
            g_wvCtrl->put_Bounds(rc);
        }
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH hbr = CreateSolidBrush(RGB(10, 8, 6));
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_NCCALCSIZE:
        // Make the entire window rect the client area so no caption is visible,
        // while keeping WS_CAPTION in the style so DWM animates minimize/restore.
        if (wp) return 0;
        break;

    case WM_NCACTIVATE:
        // Prevent Windows from painting a caption bar on activation changes.
        return TRUE;

    case WM_COMMAND:
    {
        WORD id = LOWORD(wp);

        if (id == ID_LINK_WEBSITE) {
            ShellExecuteW(nullptr, L"open", L"https://wow-hc.com", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }

        if (id == ID_LINK_ADDONS) {
            ShellExecuteW(nullptr, L"open", L"https://wow-hc.com/addons/classic", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }

        // Realm selection is handled via JS bridge (HandleWebMessage "setRealm").

        if (id == ID_BTN_OPEN) {
            std::wstring era = g_clientPath + L"\\_classic_era_";
            std::wstring openPath = (GetFileAttributesW(era.c_str()) != INVALID_FILE_ATTRIBUTES)
                ? era : g_clientPath;
            ShellExecuteW(nullptr, L"explore", openPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }

        if (id == ID_BTN_BROWSE) {
            {
                bool doFolderPick = false;
                while (true) {
                    int modeChoice = WaitForModalResponse(L"installMode");
                    if (modeChoice == 0) break;          // cancelled
                    if (modeChoice == 1) {               // new install → pick version
                        int verChoice = WaitForModalResponse(L"versionPicker");
                        if (verChoice == 0) continue;    // back → re-show installMode
                        g_pendingInstallType     = (verChoice == 2) ? CT_112 : CT_114;
                        g_pendingExistingInstall = false;
                        doFolderPick = true;
                        break;
                    } else {                             // existing install
                        g_pendingExistingInstall = true;
                        g_pendingInstallType     = CT_UNKNOWN;
                        doFolderPick = true;
                        break;
                    }
                }
                if (!doFolderPick) break;
            }
            IFileOpenDialog* pDlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg)))) {
                DWORD opts = 0; pDlg->GetOptions(&opts);
                pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                {
                    std::wstring dlgTitle;
                    if (g_pendingExistingInstall) {
                        dlgTitle = L"Select your existing WoW installation folder";
                        g_pendingExistingInstall = false;
                    } else if (g_pendingInstallType == CT_114) {
                        dlgTitle = L"Select a folder to install WoW Classic Era 1.14.2";
                    } else if (g_pendingInstallType == CT_112) {
                        dlgTitle = L"Select a folder to install WoW Vanilla 1.12.1";
                    } else {
                        dlgTitle = L"Select your WoW installation folder, or an empty folder for a new installation";
                        g_pendingExistingInstall = false;
                    }
                    pDlg->SetTitle(dlgTitle.c_str());
                }
                if (!g_clientPath.empty()) {
                    IShellItem* pInit = nullptr;
                    if (SUCCEEDED(SHCreateItemFromParsingName(g_clientPath.c_str(), nullptr, IID_PPV_ARGS(&pInit)))) {
                        pDlg->SetFolder(pInit);
                        pInit->Release();
                    }
                }
                if (SUCCEEDED(pDlg->Show(hwnd))) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                        wchar_t* path = nullptr;
                        pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
                        if (path) {
                            std::wstring selected = path;
                            CoTaskMemFree(path);

                            // Detect if the launcher exe is sitting inside the selected folder
                            {
                                wchar_t exeBuf[MAX_PATH] = {};
                                GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
                                std::wstring exeDir = exeBuf;
                                auto slash = exeDir.find_last_of(L"\\/");
                                if (slash != std::wstring::npos) exeDir.resize(slash);
                                std::wstring sel = selected;
                                if (!sel.empty() && (sel.back() == L'\\' || sel.back() == L'/')) sel.pop_back();
                                if (sel.size() == exeDir.size() && _wcsicmp(sel.c_str(), exeDir.c_str()) == 0) {
                                    MessageBoxW(hwnd,
                                        L"The WOW HC Launcher is located inside the folder you selected.\r\n\r\n"
                                        L"Please move the launcher (WOW-HC-Launcher.exe) somewhere else first "
                                        L"(e.g. your Desktop or Downloads folder)"
                                        L"for the WoW installation.",
                                        L"Launcher Inside Installation Folder", MB_OK | MB_ICONERROR);
                                    pItem->Release();
                                    pDlg->Release();
                                    break;
                                }
                            }

                            WowInstallInfo info;
                            if (FindWowInstall(selected, info)) {
                                // ── Existing WoW installation detected ──────────
                                if (info.type == CT_114) {
                                    int verMaj=0, verMin=0, verPatch=0;
                                    bool gotVer = GetExeVersion(info.wowExePath, verMaj, verMin, verPatch);
                                    bool versionOk = gotVer && (verMaj == 1 && verMin == 14 && verPatch == 2);

                                    if (gotVer && !versionOk) {
                                        wchar_t msg[320];
                                        swprintf_s(msg,
                                            L"This WoW installation (version %d.%d.%d) is not compatible.\r\n\r\n"
                                            L"Only WoW Classic Era 1.14.2 is supported for this client type.\r\n"
                                            L"Please select an empty folder for a fresh installation instead.",
                                            verMaj, verMin, verPatch);
                                        MessageBoxW(hwnd, msg, L"Incompatible Installation", MB_OK | MB_ICONERROR);
                                    } else if (!gotVer) {
                                        int ans = MessageBoxW(hwnd,
                                            L"Could not read the version of WowClassic.exe.\r\n\r\n"
                                            L"Continue using this installation?\r\n"
                                            L"(Only WoW Classic Era 1.14.2 is compatible)",
                                            L"Version Unknown", MB_YESNO | MB_ICONWARNING);
                                        versionOk = (ans == IDYES);
                                    }

                                    if (versionOk) {
                                        std::wstring foundHermes  = FindExeNearby(info.clientDir, L"HermesProxy.exe");
                                        std::wstring foundArctium = FindExeNearby(info.clientDir, L"Arctium WoW Launcher.exe");

                                        if (foundHermes.empty() || foundArctium.empty()) {
                                            std::wstring missing;
                                            if (foundHermes.empty())  missing += L"  - HermesProxy.exe\r\n";
                                            if (foundArctium.empty()) missing += L"  - Arctium WoW Launcher.exe\r\n";
                                            std::wstring mbMsg =
                                                std::wstring(L"The following file(s) were not found in or near the selected folder:\r\n\r\n")
                                                + missing
                                                + L"\r\nIf they are installed in a parent folder, please select that folder instead.\r\n"
                                                  L"Otherwise choose an empty folder and the launcher will install everything.";
                                            MessageBoxW(hwnd, mbMsg.c_str(), L"Missing Files", MB_OK | MB_ICONWARNING);
                                        } else {
                                            bool pathChanged    = (g_clientPath != info.clientDir);
                                            g_clientPath        = info.clientDir;
                                            g_installPath       = info.clientDir;
                                            g_hermesExePath     = foundHermes;
                                            g_arctiumExePath    = foundArctium;
                                            g_clientType        = CT_114;
                                            g_wowTweakedExePath.clear();
                                            PostStateToWebView();
                                            if (pathChanged) {
                                                ResetLastCheckTime();
                                                SaveConfig();
                                            }
                                            if (!g_workerBusy.load() && (pathChanged || !g_playReady.load())) {
                                                g_workerBusy = true;
                                                std::thread(Worker).detach();
                                            }
                                            RefreshPlayButton();
                                        }
                                    }
                                } else { // CT_112
                                    int verMaj=0, verMin=0, verPatch=0;
                                    bool gotVer = GetExeVersion(info.wowExePath, verMaj, verMin, verPatch);
                                    bool versionOk = !gotVer || (verMaj == 1 && verMin == 12);

                                    if (gotVer && !versionOk) {
                                        wchar_t msg[320];
                                        swprintf_s(msg,
                                            L"Wow_tweaked.exe version %d.%d.%d does not appear to be 1.12.x.\r\n\r\n"
                                            L"Continue using this installation anyway?",
                                            verMaj, verMin, verPatch);
                                        versionOk = (MessageBoxW(hwnd, msg,
                                            L"Version Mismatch", MB_YESNO | MB_ICONWARNING) == IDYES);
                                    }

                                    if (versionOk) {
                                        bool pathChanged    = (g_clientPath != info.clientDir);
                                        g_clientPath        = info.clientDir;
                                        g_installPath       = info.clientDir;
                                        g_clientType        = CT_112;
                                        g_hermesExePath.clear();
                                        g_arctiumExePath.clear();
                                        g_wowTweakedExePath = info.wowExePath;
                                        PostStateToWebView();
                                        if (pathChanged) {
                                            ResetLastCheckTime();
                                            SaveConfig();
                                        }
                                        if (!g_workerBusy.load() && (pathChanged || !g_playReady.load())) {
                                            g_workerBusy = true;
                                            std::thread(Worker).detach();
                                        }
                                        RefreshPlayButton();
                                    }
                                }
                            } else {
                                // ── No WoW found — new installation ─────────────
                                // Determine version (from startup flow or ask now)
                                ClientType newType = g_pendingInstallType;
                                g_pendingInstallType = CT_UNKNOWN;
                                if (newType == CT_UNKNOWN) {
                                    int verChoice = WaitForModalResponse(L"versionPicker");
                                    if (verChoice == 0) {
                                        pItem->Release();
                                        pDlg->Release();
                                        break;
                                    }
                                    newType = (verChoice == 2) ? CT_112 : CT_114;
                                }

                                std::wstring effectivePath = selected + L"\\WOW-HC";
                                if (g_installPath != effectivePath) {
                                    WIN32_FIND_DATAW findData;
                                    HANDLE hFind = FindFirstFileW(
                                        (effectivePath + L"\\*").c_str(), &findData);
                                    bool isEmpty = true;
                                    if (hFind != INVALID_HANDLE_VALUE) {
                                        do {
                                            if (wcscmp(findData.cFileName, L".") != 0 &&
                                                wcscmp(findData.cFileName, L"..") != 0) {
                                                isEmpty = false; break;
                                            }
                                        } while (FindNextFileW(hFind, &findData));
                                        FindClose(hFind);
                                    }
                                    if (!isEmpty) {
                                        MessageBoxW(hwnd,
                                            L"The 'WOW-HC' folder inside the selected location is not empty.\r\n\r\n"
                                            L"Please select a different location or clear the existing WOW-HC folder.",
                                            L"Folder Not Empty", MB_OK | MB_ICONWARNING);
                                    } else {
                                        g_clientType        = newType;
                                        g_installPath       = effectivePath;
                                        g_clientPath        = effectivePath + L"\\client";
                                        g_wowTweakedExePath.clear();
                                        if (newType == CT_114) {
                                            g_hermesExePath  = g_clientPath + L"\\HermesProxy.exe";
                                            g_arctiumExePath = g_clientPath + L"\\Arctium WoW Launcher.exe";
                                        } else {
                                            g_hermesExePath.clear();
                                            g_arctiumExePath.clear();
                                        }
                                        PostStateToWebView();
                                        ResetLastCheckTime();
                                        SaveConfig();
                                        if (!g_workerBusy.load()) {
                                            g_workerBusy = true;
                                            std::thread(Worker).detach();
                                        }
                                        RefreshPlayButton();
                                    }
                                }
                            }
                        }
                        pItem->Release();
                    }
                }
                pDlg->Release();
            }
        }
        else if (id == ID_BTN_PLAY) {
            if (g_workerBusy.load()) break;

            bool installed = g_clientInstalled.load();

            if (!installed && !g_clientPath.empty()) {
                g_workerBusy = true;
                g_playReady  = false;
                RefreshPlayButton();
                std::thread(Worker).detach();
            }
            else if (installed && g_playReady.load()) {
                g_isLaunching = true;
                if (RB_GetSettings().autoStartOnPlay && !RB_IsRunning()) {
                    EnsureRecordingSaveFolder(hwnd);
                    RB_Start();
                }
                PostStatus(WS_LAUNCHING);

                if (g_clientType == CT_112) {
                    // ── 1.12.1: launch Wow_tweaked.exe directly ─────────────
                    std::wstring wowExe = g_wowTweakedExePath;
                    if (wowExe.empty()) {
                        std::wstring def = g_clientPath + L"\\_classic_era_\\Wow_tweaked.exe";
                        if (GetFileAttributesW(def.c_str()) != INVALID_FILE_ATTRIBUTES)
                            wowExe = def;
                        else {
                            std::wstring found = FindExeInTree(g_clientPath, L"Wow_tweaked.exe", 3);
                            if (!found.empty()) wowExe = found;
                        }
                        if (!wowExe.empty()) { g_wowTweakedExePath = wowExe; SaveConfig(); }
                    }
                    std::thread([wowExe]() {
                        if (!wowExe.empty()) {
                            LaunchExe(wowExe, L"");
                        } else {
                            MessageBoxW(g_hwnd,
                                L"Wow_tweaked.exe was not found in the client folder.\r\n"
                                L"Please re-select your installation path.",
                                L"Launch Error", MB_OK | MB_ICONERROR);
                        }
                        PostMessageW(g_hwnd, WM_WORKER_DONE, 1, 0);
                    }).detach();
                } else {
                    // ── 1.14.2: start HermesProxy + Arctium ─────────────────
                    std::wstring hermesExe  = g_hermesExePath;
                    std::wstring arctiumExe = g_arctiumExePath;
                    std::wstring clientPath = g_clientPath;
                    std::thread([hermesExe, arctiumExe, clientPath]() {

                        if (IsProcessRunning(L"WowClassic.exe")) {
                            KillProcess(L"WowClassic.exe");
                            Sleep(500);
                        }
                        if (IsProcessRunning(L"HermesProxy.exe")) {
                            KillProcess(L"HermesProxy.exe");
                            Sleep(500);
                        }
                        if (IsPortInUse(8084)) {
                            MessageBoxW(g_hwnd,
                                L"Port 8084 is already in use by another application.\r\n"
                                L"HermesProxy needs this port to run.\r\n\r\n"
                                L"Please close the application using port 8084 and try again.",
                                L"Port 8084 Unavailable", MB_OK | MB_ICONWARNING);
                            PostMessageW(g_hwnd, WM_WORKER_DONE, 1, 0);
                            return;
                        }
                        PatchConfigWtfPortal(clientPath);
                        SetClientFilesReadOnly(clientPath, true);
                        LaunchHermesWithPipe(hermesExe);
                        Sleep(1500);
                        // Keep Arctium's handle so we know exactly which process we started
                        // and can find the WowClassic.exe it spawns by parent PID.
                        HANDLE hArctium = LaunchExeGetHandle(arctiumExe, L"--staticseed --version=ClassicEra");
                        Sleep(2000);
                        PostMessageW(g_hwnd, WM_WORKER_DONE, 1, 0);

                        if (hArctium) {
                            DWORD arctiumPid = GetProcessId(hArctium);
                            std::thread([hArctium, arctiumPid]() {
                                // Wait up to 60s for the WowClassic.exe that OUR Arctium spawned.
                                // Keeping hArctium open prevents PID reuse until we've found the child.
                                DWORD wowPid = WaitForChildProcess(arctiumPid, L"WowClassic.exe", 60000);
                                CloseHandle(hArctium);
                                if (!wowPid) return;

                                HANDLE hWow = OpenProcess(SYNCHRONIZE, FALSE, wowPid);
                                if (!hWow) return;
                                WaitForSingleObject(hWow, INFINITE);
                                CloseHandle(hWow);

                                // The specific WowClassic.exe we tracked has closed — stop HermesProxy.
                                HANDLE hH = g_hermesProcess;
                                if (hH) TerminateProcess(hH, 0);
                                PostMessageW(g_hwnd, WM_HERMES_CLOSED, 0, 0);
                            }).detach();
                        }
                    }).detach();
                }
            }
        }
        else if (id == ID_BTN_RECORD) {
            if (RB_IsRunning()) {
                if (RB_GetSettings().promptSaveOnStop) {
                    int r = MessageBoxW(hwnd, L"Save the replay before stopping?",
                        L"Save Replay", MB_YESNOCANCEL | MB_ICONQUESTION);
                    if (r == IDCANCEL) break;
                    if (r == IDYES) { RB_SaveNow(); Sleep(100); }
                }
                RB_Stop();
            } else {
                if (!EnsureRecordingSaveFolder(hwnd)) break;
                if (!RB_Start())
                    std::thread(CheckAndBootstrapFFmpegDlls).detach();
            }
        }
        else if (id == ID_BTN_RECORD_SETTINGS) {
            PostMessageW(hwnd, WM_REC_SETTINGS_OPEN, 0, 0);
        }
        else if (id == ID_BTN_SAVE_REPLAY) {
            RbSaveResult sr = RB_SaveNow();
            if (g_showRecordingNotifications && g_webview && g_wvReady) {
                if (sr == RB_SAVE_OK)
                    g_webview->PostWebMessageAsJson(L"{\"type\":\"notification\",\"text\":\"Replay saved\"}");
                else if (sr == RB_SAVE_TOO_EARLY)
                    g_webview->PostWebMessageAsJson(L"{\"type\":\"notification\",\"text\":\"Recording just started, try again in a few seconds.\"}");
            }
        }
        else if (id == ID_BTN_UPLOAD) {
            ShowUploadWindow(hwnd, g_configDir);
        }
        else if (id == ID_BTN_TRANSFER) {
            if (g_workerBusy.load()) break;
            if (!g_clientInstalled.load()) break;

            // Loop until user picks a valid _classic_era_ folder or cancels
            while (true) {
                IFileOpenDialog* pDlg = nullptr;
                bool cancelled = true;
                std::wstring picked;

                if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg)))) {
                    DWORD opts = 0; pDlg->GetOptions(&opts);
                    pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                    pDlg->SetTitle(L"Select your old WoW installation folder (any parent of/or _classic_era_ folder works)");
                    if (SUCCEEDED(pDlg->Show(hwnd))) {
                        IShellItem* pItem = nullptr;
                        if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                            wchar_t* path = nullptr;
                            pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
                            if (path) { picked = path; CoTaskMemFree(path); cancelled = false; }
                            pItem->Release();
                        }
                    }
                    pDlg->Release();
                }

                if (cancelled) break;

                // Search for _classic_era_ in/under the selected folder (same depth logic as Browse)
                WowInstallInfo xferInfo;
                if (FindWowInstall(picked, xferInfo)) {
                    // Block cross-version transfer: 1.12.1 <-> 1.14.2 are incompatible
                    if (xferInfo.type != g_clientType) {
                        const wchar_t* srcVer = (xferInfo.type == CT_112) ? L"1.12.1" : L"1.14.2";
                        const wchar_t* dstVer = (g_clientType == CT_112) ? L"1.12.1" : L"1.14.2";
                        wchar_t msg[512];
                        swprintf_s(msg,
                            L"Cannot transfer: the selected installation is version %s,\n"
                            L"but your current installation is version %s.\n\n"
                            L"Addons, macros, UI layouts, and settings are NOT compatible\n"
                            L"between 1.12.1 and 1.14.2. Please select a %s installation.",
                            srcVer, dstVer, dstVer);
                        MessageBoxW(hwnd, msg, L"Incompatible Versions", MB_OK | MB_ICONERROR);
                        continue; // reopen the folder picker
                    }
                    std::wstring classicEraDir = xferInfo.clientDir + L"\\_classic_era_";
                    // Check that this is a 1.14.x (Classic Era) build
                    int verMaj = 0, verMin = 0, verPatch = 0;
                    if (GetExeVersion(xferInfo.wowExePath, verMaj, verMin, verPatch)
                            && !(verMaj == 1 && verMin == 14)) {
                        wchar_t msg[320];
                        swprintf_s(msg,
                            L"This does not appear to be a WoW Classic Era (1.14.x) installation.\n"
                            L"Detected version: %d.%d.%d\n\n"
                            L"Transferring settings from a different version may not work correctly.\n"
                            L"Continue anyway?",
                            verMaj, verMin, verPatch);
                        if (MessageBoxW(hwnd, msg, L"Version Mismatch",
                                MB_YESNO | MB_ICONWARNING) != IDYES)
                            break;
                    }
                    g_workerBusy = true;
                    RefreshPlayButton();
                    std::thread([classicEraDir]() { TransferWorker(classicEraDir); }).detach();
                    break;
                }

                MessageBoxW(hwnd,
                    L"A WoW Classic Era installation was not found in or under the selected folder.\n\n"
                    L"You can select the _classic_era_ folder itself, its parent,\n"
                    L"or the install root that contains it.",
                    L"Installation Not Found",
                    MB_OK | MB_ICONWARNING);
                // Loop reopens the dialog
            }
        }
        break;
    }

    case WM_HOTKEY:
        if (wp == HOTKEY_ID_RB_STARTSTOP) {
            if (RB_IsRunning()) {
                if (RB_GetSettings().promptSaveOnStop) {
                    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    int r = MessageBoxW(hwnd, L"Save the replay before stopping?",
                        L"Save Replay", MB_YESNOCANCEL | MB_ICONQUESTION);
                    if (r == IDCANCEL) return 0;
                    if (r == IDYES) { RB_SaveNow(); Sleep(100); }
                }
                RB_Stop();
            } else {
                if (!EnsureRecordingSaveFolder(hwnd)) return 0;
                if (!RB_Start())
                    std::thread(CheckAndBootstrapFFmpegDlls).detach();
            }
        } else if (wp == HOTKEY_ID_RB_SAVE) {
            RB_SaveNow();
        }
        return 0;

    case WM_RB_STATUS:
        if (wp) {
            if (!g_hIconRecordingOverlay) g_hIconRecordingOverlay = CreateRecordingOverlayIcon(GetSystemMetrics(SM_CXSMICON));
            if (g_pTaskbar) g_pTaskbar->SetOverlayIcon(hwnd, g_hIconRecordingOverlay, L"Recording");
        } else {
            if (g_pTaskbar) g_pTaskbar->SetOverlayIcon(hwnd, nullptr, nullptr);
        }
        PostStateToWebView();
        return 0;

    case WM_APP + 30:
        SaveReplaySettings(RB_GetSettings(), ConfigPath());
        return 0;

    case WM_REC_SETTINGS_OPEN:
        PostShowModal(L"recordSettings");
        PostRecordSettingsStateToWebView();
        return 0;

    case WM_REC_SETTINGS_BROWSE:
    {
        IFileOpenDialog* pDlg = nullptr;
        std::wstring chosen;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg)))) {
            DWORD opts = 0;
            pDlg->GetOptions(&opts);
            pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            pDlg->SetTitle(L"Choose folder to save replays in");
            std::wstring cur = RB_GetSettings().saveFolder;
            if (!cur.empty()) {
                IShellItem* pInit = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(cur.c_str(), nullptr, IID_PPV_ARGS(&pInit)))) {
                    pDlg->SetFolder(pInit);
                    pInit->Release();
                }
            }
            if (SUCCEEDED(pDlg->Show(hwnd))) {
                IShellItem* pItem = nullptr;
                if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                    wchar_t* path = nullptr;
                    pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
                    if (path) { chosen = path; CoTaskMemFree(path); }
                    pItem->Release();
                }
            }
            pDlg->Release();
        }
        if (!chosen.empty() && g_webview && g_wvReady) {
            std::wstring j2 = L"{\"type\":\"recordSettingsFolderChosen\",\"folder\":\""
                            + JsonEscW(chosen) + L"\"}";
            g_webview->PostWebMessageAsJson(j2.c_str());
        }
        return 0;
    }

    case WM_REC_SETTINGS_TOGGLE:
    {
        std::string* ps = reinterpret_cast<std::string*>(lp);
        std::string body = ps ? *ps : "";
        delete ps;

        ReplaySettings s = RB_GetSettings();
        s.monitorIndex    = JsonInt(body, "monitorIndex", s.monitorIndex);
        s.minutes         = std::max(1, std::min(60, JsonInt(body, "minutes", s.minutes)));
        s.fps             = std::max(20, std::min(60, JsonInt(body, "fps", s.fps)));
        s.saveFolder      = JsonStringW(body, "saveFolder");
        s.promptSaveOnStop = JsonBool(body, "promptSaveOnStop", s.promptSaveOnStop);
        s.autoStartOnPlay  = JsonBool(body, "autoStartOnPlay",  s.autoStartOnPlay);
        s.startStopVK    = (UINT)JsonInt(body, "startStopVK");
        s.startStopMods  = (UINT)JsonInt(body, "startStopMods");
        s.saveVK         = (UINT)JsonInt(body, "saveVK");
        s.saveMods       = (UINT)JsonInt(body, "saveMods");

        RB_SetSettings(s);
        RB_UnregisterHotkeys();
        int hkFail = RB_RegisterHotkeys();
        if (hkFail & RB_HK_STARTSTOP_FAILED) {
            s.startStopVK = 0; s.startStopMods = 0;
            RB_SetSettings(s);
            if (g_webview && g_wvReady)
                g_webview->PostWebMessageAsJson(
                    L"{\"type\":\"recordSettingsConflict\",\"field\":\"startStop\"}");
        }
        if (hkFail & RB_HK_SAVE_FAILED) {
            s.saveVK = 0; s.saveMods = 0;
            RB_SetSettings(s);
            if (g_webview && g_wvReady)
                g_webview->PostWebMessageAsJson(
                    L"{\"type\":\"recordSettingsConflict\",\"field\":\"save\"}");
        }

        if (RB_IsRunning()) {
            if (RB_GetSettings().promptSaveOnStop) {
                int r = MessageBoxW(hwnd, L"Save the replay before stopping?",
                    L"Save Replay", MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDCANCEL) { PostStateToWebView(); return 0; }
                if (r == IDYES) { RB_SaveNow(); Sleep(100); }
            }
            RB_Stop();
        } else {
            EnsureRecordingSaveFolder(hwnd);
            RB_Start();
        }
        PostStateToWebView();
        return 0;
    }

    case WM_REC_SETTINGS_CLOSE:
    {
        std::string* ps = reinterpret_cast<std::string*>(lp);
        std::string body = ps ? *ps : "";
        delete ps;

        ReplaySettings s = RB_GetSettings();
        s.monitorIndex    = JsonInt(body, "monitorIndex", s.monitorIndex);
        s.minutes         = std::max(1, std::min(60, JsonInt(body, "minutes", s.minutes)));
        s.fps             = std::max(20, std::min(60, JsonInt(body, "fps", s.fps)));
        s.saveFolder      = JsonStringW(body, "saveFolder");
        s.promptSaveOnStop = JsonBool(body, "promptSaveOnStop", s.promptSaveOnStop);
        s.autoStartOnPlay  = JsonBool(body, "autoStartOnPlay",  s.autoStartOnPlay);
        s.startStopVK    = (UINT)JsonInt(body, "startStopVK");
        s.startStopMods  = (UINT)JsonInt(body, "startStopMods");
        s.saveVK         = (UINT)JsonInt(body, "saveVK");
        s.saveMods       = (UINT)JsonInt(body, "saveMods");

        RB_SetSettings(s);
        int hkFail = RB_RegisterHotkeys();

        if (hkFail & RB_HK_STARTSTOP_FAILED) {
            s.startStopVK = 0; s.startStopMods = 0;
            RB_SetSettings(s);
            SaveReplaySettings(RB_GetSettings(), ConfigPath());
            if (g_webview && g_wvReady)
                g_webview->PostWebMessageAsJson(
                    L"{\"type\":\"recordSettingsConflict\",\"field\":\"startStop\"}");
            return 0;
        }
        if (hkFail & RB_HK_SAVE_FAILED) {
            s.saveVK = 0; s.saveMods = 0;
            RB_SetSettings(s);
            SaveReplaySettings(RB_GetSettings(), ConfigPath());
            if (g_webview && g_wvReady)
                g_webview->PostWebMessageAsJson(
                    L"{\"type\":\"recordSettingsConflict\",\"field\":\"save\"}");
            return 0;
        }

        SaveReplaySettings(RB_GetSettings(), ConfigPath());
        if (g_webview && g_wvReady)
            g_webview->PostWebMessageAsJson(L"{\"type\":\"hideModal\"}");
        PostStateToWebView();
        return 0;
    }

    case WM_GEN_SETTINGS_CLOSE:
    {
        std::string* ps = reinterpret_cast<std::string*>(lp);
        std::string body = ps ? *ps : "";
        delete ps;

        g_showRecordingNotifications = JsonBool(body, "showRecordingNotifications", g_showRecordingNotifications);
        RB_SetShowNotifications(g_showRecordingNotifications);
        SaveConfig();
        if (g_webview && g_wvReady)
            g_webview->PostWebMessageAsJson(L"{\"type\":\"hideModal\"}");
        PostStateToWebView();
        return 0;
    }

    case WM_WORKER_STATUS:
    {
        int s = (int)wp;
        if (s >= 0 && s <= WS_NO_PATH) {
            g_currentStatus = STATUS_TEXT[s];
            PostStateToWebView();
        }
        break;
    }

    case WM_WORKER_TEXT:
    {
        auto* str = reinterpret_cast<std::wstring*>(lp);
        g_currentStatus = *str;
        delete str;
        PostStateToWebView();
        break;
    }

    case WM_WORKER_PROGRESS:
    {
        g_currentProgress = (int)wp;
        g_taskbarLastPct  = (int)wp;
        if (g_workerBusy.load()) {
            g_taskbarHasProgress = true;
            if (g_pTaskbar) {
                g_pTaskbar->SetProgressState(g_hwnd, TBPF_NORMAL);
                g_pTaskbar->SetProgressValue(g_hwnd, wp, 100);
            }
        }
        PostStateToWebView();
        break;
    }

    case WM_WORKER_DONE:
    {
        bool ok = (wp != 0) && !g_clientPath.empty();
        g_isLaunching = false;
        g_playReady = ok;
        if (ok) g_clientInstalled = true;
        if (ok && g_clientType == CT_114) SetClientFilesReadOnly(g_clientPath, true);
        g_taskbarHasProgress = false;
        RefreshPlayButton();
        if (ok) PostStatus(WS_READY);
        if (ok) EnsureRecordingSaveFolder(hwnd);
        if (ok) UpdateRealmConfig(g_realmIndex);
        if (g_pTaskbar)
            g_pTaskbar->SetProgressState(g_hwnd, ok ? TBPF_NOPROGRESS : TBPF_ERROR);
        if (ok) RefreshVersionLabels();

        if (ok && g_freshInstall) {
            g_freshInstall = false;
            EnsureDesktopShortcut(false);
            int resp = MessageBoxW(hwnd,
                L"Client installed successfully!\r\n\r\n"
                L"Do you have an existing WoW installation you'd like to\r\n"
                L"transfer your UI, macros, addons, and settings from?",
                L"Transfer from existing installation?",
                MB_YESNO | MB_ICONQUESTION);
            if (resp == IDYES)
                PostMessageW(hwnd, WM_COMMAND,
                    MAKEWPARAM(ID_BTN_TRANSFER, BN_CLICKED), 0);
        }
        break;
    }

    case WM_TRANSFER_DONE:
    {
        bool ok = (wp != 0);
        g_taskbarHasProgress = false;
        RefreshPlayButton();
        if (g_pTaskbar)
            g_pTaskbar->SetProgressState(g_hwnd, ok ? TBPF_NOPROGRESS : TBPF_ERROR);
        if (!ok)
            MessageBoxW(hwnd,
                L"Transfer failed. Some files may not have been copied.\n"
                L"Check that the source folder is accessible.",
                L"Transfer Error", MB_OK | MB_ICONERROR);
        break;
    }

    case WM_SET_INSTALL_MODE:
    {
        g_clientInstalled = (wp != 0);
        PostStateToWebView();
        break;
    }

    case WM_TIMER:
        if (wp == ID_TIMER_HOVER) {
            KillTimer(hwnd, ID_TIMER_HOVER); // hover animations now in CSS/JS
            return 0;
        }
        if (wp == ID_TIMER_UPDATE && !g_workerBusy.load())
            std::thread(PeriodicUpdateCheck).detach();
        if (wp == ID_TIMER_LIVE)
            std::thread(FetchLiveData).detach();
        return 0;

    case WM_ASK_UPDATE:
    {
        auto* payload = reinterpret_cast<std::wstring*>(lp);
        size_t nl = payload->find(L'\n');
        std::wstring remote = payload->substr(0, nl);
        std::wstring local  = (nl != std::wstring::npos) ? payload->substr(nl + 1) : L"";
        delete payload;

        static const wchar_t* names[] = { L"HermesProxy", L"WOW_HC Addon", L"WOW HC Launcher" };
        const wchar_t* name = (wp < 3) ? names[wp] : L"Component";
        wchar_t msg[512];
        if (local.empty())
            swprintf_s(msg, L"%s %s is available.\r\nInstall now?", name, remote.c_str());
        else
            swprintf_s(msg, L"%s %s is available (installed: %s).\r\nUpdate now?",
                name, remote.c_str(), local.c_str());
        return MessageBoxW(hwnd, msg, L"Update Available", MB_YESNO | MB_ICONQUESTION);
    }

    case WM_APPLY_SELF_UPD:
    {
        auto* path = reinterpret_cast<std::wstring*>(lp);
        std::wstring newExe = *path;
        delete path;
        ApplyLauncherUpdate(newExe);
        return 0;
    }

    case WM_STARTUP_CHECK_DONE:
    {
        AppendLog(L"WM_STARTUP_CHECK_DONE: clientPath='%s' clientType=%d g_wvReady=%d",
            g_clientPath.c_str(), (int)g_clientType, (int)g_wvReady);
        // Startup launcher-update check is done — proceed with normal startup flow.
        if (!g_clientPath.empty()) {
            AppendLog(L"WM_STARTUP_CHECK_DONE: has path, starting Worker");
            UpdateRealmConfig(g_realmIndex);
            g_workerBusy = true;
            PostStateToWebView();
            std::thread(Worker).detach();
        } else {
            AppendLog(L"WM_STARTUP_CHECK_DONE: no path, showing browse dialog");
            g_currentStatus = STATUS_TEXT[WS_NO_PATH];
            PostStateToWebView();
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_BROWSE, BN_CLICKED), 0);
        }
        return 0;
    }

    case WM_DRAWITEM:
        // No owner-draw controls — all UI is in WebView2.
        break;

    case WM_HERMES_LINE:
    {
        auto* ws = reinterpret_cast<std::wstring*>(lp);
        if (!ws) break;

        if (!g_showConsole) {
            g_showConsole = true;
            PostStateToWebView();
        }

        ++g_consoleLineCount;
        AppendLog(L"[Hermes] %s", StripAnsiW(*ws).c_str());
        PostHermesLineToWebView(*ws);

        delete ws;
        break;
    }

    case WM_HERMES_CLOSED:
        AppendLog(L"[Hermes] --- process closed ---");
        g_showConsole    = false;
        g_consoleLineCount = 0;
        PostStateToWebView();
        break;

    case WM_LIVE_DATA_JSON:
    {
        auto* msg = reinterpret_cast<std::wstring*>(lp);
        if (msg) {
            // Check server-enforced minimum launcher version (serverStats only, once per session)
            static bool s_minVerChecked = false;
            if (!s_minVerChecked && !IsDevBuild() && msg->find(L"\"serverStats\"") != std::wstring::npos) {
                s_minVerChecked = true;
                int nlen = WideCharToMultiByte(CP_UTF8, 0, msg->c_str(), (int)msg->size(), nullptr, 0, nullptr, nullptr);
                std::string narrow(nlen, '\0');
                WideCharToMultiByte(CP_UTF8, 0, msg->c_str(), (int)msg->size(), narrow.data(), nlen, nullptr, nullptr);
                std::string minVer = JsonString(narrow, "launcher_min_version");
                if (!minVer.empty()) {
                    std::wstring minVerW(minVer.begin(), minVer.end());
                    if (IsNewer(GetLauncherVersion(), minVerW))
                        std::thread([]() { RunLauncherUpdateCheck(true); }).detach();
                }
            }
            if (g_webview && g_wvReady) g_webview->PostWebMessageAsJson(msg->c_str());
            delete msg;
        }
        break;
    }

    case WM_SET_REALM:
    {
        int idx = (int)wp;
        if (idx == 1) PostShowModal(L"ptr");
        UpdateRealmConfig(idx);
        PostStateToWebView();
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        // WS_POPUP: window size == client size (no NC area)
        LONG w = MulDiv(WND_CLIENT_W, g_dpi, 96);
        LONG h = MulDiv(WND_CLIENT_H, g_dpi, 96);
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = {w, h};
        mmi->ptMaxTrackSize = {w, h};
        break;
    }

    case WM_CLOSE:
    {
        if (RB_IsRunning()) {
            int r = MessageBoxW(hwnd,
                L"The screen recorder is still running.\n\nQuit anyway?",
                L"Recorder Running", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
            if (r != IDYES) return 0;
        } else {
            const wchar_t* gameExe = (g_clientType == CT_112) ? L"Wow_tweaked.exe" : L"WowClassic.exe";
            if (IsProcessRunning(gameExe)) {
                int r = MessageBoxW(hwnd,
                    L"The game is still running.\n\nQuit the launcher anyway?",
                    L"Game Running", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                if (r != IDYES) return 0;
            }
        }
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
        g_wvCtrl.Reset();
        g_webview.Reset();
        RB_Shutdown();
        KillTimer(hwnd, ID_TIMER_UPDATE);
        KillTimer(hwnd, ID_TIMER_LIVE);
        if (g_hermesProcess)  { TerminateProcess(g_hermesProcess, 0); CloseHandle(g_hermesProcess); g_hermesProcess = nullptr; }
        if (g_hermesPipeRead) { CloseHandle(g_hermesPipeRead); g_hermesPipeRead = nullptr; }
        if (g_pTaskbar) { g_pTaskbar->Release(); g_pTaskbar = nullptr; }
        if (g_hIconLarge)            { DestroyIcon(g_hIconLarge);            g_hIconLarge            = nullptr; }
        if (g_hIconSmall)            { DestroyIcon(g_hIconSmall);            g_hIconSmall            = nullptr; }
        if (g_hIconRecordingOverlay) { DestroyIcon(g_hIconRecordingOverlay); g_hIconRecordingOverlay = nullptr; }
        PostQuitMessage(0);
        break;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Desktop shortcut ───────────────────────────────────────────────────────────
static void SaveIconFile(const std::wstring& icoPath)
{
    HRSRC hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(IDR_LOGO_ROUND), L"PNG");
    if (!hRes) return;
    HGLOBAL hMem = LoadResource(g_hInst, hRes);
    if (!hMem) return;
    void* pData = LockResource(hMem);
    DWORD size  = SizeofResource(g_hInst, hRes);
    if (!pData || !size) return;

    HANDLE hFile = CreateFileW(icoPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD written; WORD w; DWORD dw; BYTE b;
    // ICONDIR
    w = 0; WriteFile(hFile, &w, 2, &written, nullptr);
    w = 1; WriteFile(hFile, &w, 2, &written, nullptr);
    w = 1; WriteFile(hFile, &w, 2, &written, nullptr);
    // ICONDIRENTRY
    b = 0;  WriteFile(hFile, &b, 1, &written, nullptr); // width  (0 = 256)
    b = 0;  WriteFile(hFile, &b, 1, &written, nullptr); // height (0 = 256)
    b = 0;  WriteFile(hFile, &b, 1, &written, nullptr); // colorCount
    b = 0;  WriteFile(hFile, &b, 1, &written, nullptr); // reserved
    w = 1;  WriteFile(hFile, &w, 2, &written, nullptr); // planes
    w = 32; WriteFile(hFile, &w, 2, &written, nullptr); // bitCount
    dw = size; WriteFile(hFile, &dw, 4, &written, nullptr); // bytesInRes
    dw = 22;   WriteFile(hFile, &dw, 4, &written, nullptr); // imageOffset (6+16)
    WriteFile(hFile, pData, size, &written, nullptr);
    CloseHandle(hFile);
}

// onlyIfAlreadyExists=true  → only refresh target (called at startup, skips if user deleted it)
// onlyIfAlreadyExists=false → create on first install, refresh if it already exists
static void EnsureDesktopShortcut(bool onlyIfAlreadyExists)
{
    wchar_t desktop[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop)))
        return;

    std::wstring lnkPath = std::wstring(desktop) + L"\\WOW-HC.lnk";

    wchar_t flag[8] = {};
    GetPrivateProfileStringW(L"Launcher", L"ShortcutCreated", L"0",
        flag, 8, ConfigPath().c_str());
    bool alreadyCreated = (flag[0] == L'1');
    bool lnkExists = GetFileAttributesW(lnkPath.c_str()) != INVALID_FILE_ATTRIBUTES;

    if (onlyIfAlreadyExists) {
        // Startup refresh: only update target if shortcut still exists on the desktop.
        // If the user deleted it, do nothing (respect that choice).
        if (!lnkExists) return;
    } else {
        // Post-install creation: if already created but user deleted it, respect that.
        if (alreadyCreated && !lnkExists) return;
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    exeDir = exeDir.substr(0, exeDir.rfind(L'\\'));

    IShellLinkW* psl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void**>(&psl))))
        return;

    std::wstring icoPath = g_configDir + L"\\wow-hc.ico";
    SaveIconFile(icoPath);
    psl->SetPath(exePath);
    psl->SetWorkingDirectory(exeDir.c_str());
    psl->SetIconLocation(icoPath.c_str(), 0);
    psl->SetDescription(L"WOW HC Launcher");

    IPersistFile* ppf = nullptr;
    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf)))) {
        ppf->Save(lnkPath.c_str(), TRUE);
        ppf->Release();
    }
    psl->Release();

    if (!alreadyCreated)
        WritePrivateProfileStringW(L"Launcher", L"ShortcutCreated", L"1", ConfigPath().c_str());
}

// ── UI files bootstrap ─────────────────────────────────────────────────────────
// Downloads the Full ZIP from GitHub and extracts ui/ to AppData if missing.
// Called before WebView2 init; runs synchronously on the main thread.
// Returns true if ui/ is available (or dev build using ../ui).
static bool CheckAndBootstrapUiFiles(HINSTANCE hInst)
{
    std::wstring appDataUi = g_configDir + L"\\ui";

    // Dev: ../ui (source tree) takes priority — no AppData copy needed.
    {
        std::wstring exeDir = GetExeDir();
        wchar_t full[MAX_PATH] = {};
        if (GetFullPathNameW((exeDir + L"\\..\\ui").c_str(), MAX_PATH, full, nullptr) &&
                GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES)
            return true;
    }

    // Already in AppData?
    if (GetFileAttributesW(appDataUi.c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;

    // Copy from EXE dir (user extracted the Full ZIP alongside the EXE).
    std::wstring exeUi = GetExeDir() + L"\\ui";
    if (GetFileAttributesW(exeUi.c_str()) != INVALID_FILE_ATTRIBUTES) {
        CopyUiFolder(exeUi, appDataUi);
        return GetFileAttributesW(appDataUi.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    if (IsDevBuild()) return false;

    // Nothing found: download the Full ZIP and extract ui/ into AppData.
    static const wchar_t kCls[] = L"WOWHCUiBoot";
    {
        WNDCLASSW wc     = {};
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursor(nullptr, IDC_WAIT);
        wc.lpszClassName = kCls;
        RegisterClassW(&wc);
    }

    int dlgW = MulDiv(380, g_dpi, 96);
    int dlgH = MulDiv(112, g_dpi, 96);
    HWND hDlg = CreateWindowExW(0, kCls, L"WoW HC Launcher",
        WS_POPUP | WS_CAPTION,
        (GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2,
        dlgW, dlgH, nullptr, nullptr, hInst, nullptr);

    HWND hLabel = nullptr, hBar = nullptr;
    if (hDlg) {
        RECT cr; GetClientRect(hDlg, &cr);
        int pad  = MulDiv(12, g_dpi, 96);
        int lblH = MulDiv(16, g_dpi, 96);
        int barH = MulDiv(20, g_dpi, 96);
        int cW   = cr.right - cr.left;

        hLabel = CreateWindowW(L"STATIC", L"Downloading launcher UI (first-time setup)...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, pad, cW - 2*pad, lblH,
            hDlg, nullptr, hInst, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);

        hBar = CreateWindowW(PROGRESS_CLASS, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            pad, pad + lblH + MulDiv(6, g_dpi, 96), cW - 2*pad, barH,
            hDlg, nullptr, hInst, nullptr);
        SendMessageW(hBar, PBM_SETRANGE32, 0, 100);

        ShowWindow(hDlg, SW_SHOW);
        UpdateWindow(hDlg);
    }

    auto Pump = [&]() {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    };
    auto SetPct = [&](int pct) {
        if (hBar) SendMessageW(hBar, PBM_SETPOS, pct, 0);
        Pump();
    };
    auto StartMarquee = [&]() {
        if (!hBar) return;
        SetWindowLongPtrW(hBar, GWL_STYLE,
            GetWindowLongPtrW(hBar, GWL_STYLE) | PBS_MARQUEE);
        SendMessageW(hBar, PBM_SETMARQUEE, TRUE, 40);
        Pump();
    };
    auto StopMarquee = [&]() {
        if (!hBar) return;
        SendMessageW(hBar, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongPtrW(hBar, GWL_STYLE,
            GetWindowLongPtrW(hBar, GWL_STYLE) & ~PBS_MARQUEE);
        InvalidateRect(hBar, nullptr, TRUE);
        UpdateWindow(hBar);
        Pump();
    };

    // Fetch release JSON, find Full ZIP asset URL.
    const char* verA = LAUNCHER_VERSION_STR;
    std::wstring verW(verA, verA + strlen(verA));
    std::wstring repoBase = std::wstring(L"https://api.github.com/repos/")
        + LAUNCHER_GH_OWNER + L"/" + LAUNCHER_GH_REPO;
    std::wstring apiUrls[] = {
        repoBase + L"/releases/tags/" + verW,
        repoBase + L"/releases/latest"
    };

    std::wstring tmpZip = TempFile(L"WOW-HC-Launcher-Full.zip");
    bool ok = false;
    for (const auto& apiUrl : apiUrls) {
        std::string json = HttpGet(apiUrl);
        if (json.empty()) continue;
        std::string url = FindFullZipAssetUrl(json);
        if (url.empty()) continue;
        std::wstring zipUrl(url.begin(), url.end());
        ok = HttpDownload(zipUrl, tmpZip, [&](DWORD64 dl, DWORD64 tot) {
            if (tot > 0) SetPct((int)(dl * 90 / tot));
        });
        if (ok) break;
        DeleteFileW(tmpZip.c_str());
    }

    if (!ok) {
        DeleteFileW(tmpZip.c_str());
        if (hDlg) DestroyWindow(hDlg);
        MessageBoxW(nullptr,
            L"Could not download the launcher UI files.\n\n"
            L"Check your internet connection and restart the launcher,\n"
            L"or download 'WOW-HC-Launcher-Full.zip' and extract it.",
            L"Download Failed", MB_OK | MB_ICONERROR);
        return false;
    }

    // Extract ui/ entries from the zip into AppData.
    StopMarquee();
    SetPct(90);
    if (hLabel) SetWindowTextW(hLabel, L"Installing launcher UI...");
    StartMarquee();
    Pump();

    auto escPS = [](const std::wstring& s) {
        std::wstring r;
        for (wchar_t c : s) { if (c == L'\'') r += L"''"; else r += c; }
        return r;
    };
    std::wstring script =
        L"Add-Type -AN System.IO.Compression.FileSystem\r\n"
        L"try {\r\n"
        L"  $z=[System.IO.Compression.ZipFile]::OpenRead('" + escPS(tmpZip) + L"')\r\n"
        L"  foreach($e in $z.Entries){\r\n"
        L"    if($e.FullName -like 'ui/*' -and $e.Name -ne ''){\r\n"
        L"      $rel=$e.FullName -replace '/','\\'\r\n"
        L"      $dst='" + escPS(g_configDir) + L"\\'+$rel\r\n"
        L"      $dir=[System.IO.Path]::GetDirectoryName($dst)\r\n"
        L"      if(-not(Test-Path $dir)){New-Item -ItemType Directory -Force -Path $dir|Out-Null}\r\n"
        L"      [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e,$dst,$true)\r\n"
        L"    }\r\n"
        L"  }\r\n"
        L"  $z.Dispose()\r\n"
        L"} catch { [Console]::Error.WriteLine($_); exit 1 }\r\n";

    std::wstring scriptPath = TempFile(L"launcher_ui_install.ps1");
    bool psOk = false;
    if (WritePsScript(scriptPath, script)) {
        std::wstring cmd = L"powershell.exe -NonInteractive -ExecutionPolicy Bypass -File \""
            + scriptPath + L"\"";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
        STARTUPINFOW si = {}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            DWORD wait;
            do { wait = WaitForSingleObject(pi.hProcess, 100); Pump(); }
            while (wait == WAIT_TIMEOUT);
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            psOk = (code == 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        DeleteFileW(scriptPath.c_str());
    }
    DeleteFileW(tmpZip.c_str());

    if (hDlg) DestroyWindow(hDlg);

    return psOk && GetFileAttributesW(appDataUi.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ── Entry point ────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int)
{
    g_hInst = hInst;

    if (lpCmdLine && wcsstr(lpCmdLine, L"--test"))
        g_testMode = true;
    if (lpCmdLine && wcsstr(lpCmdLine, L"--dev"))
        g_devMode = true;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_dpi = GetDpiForSystem();

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"WOWHCLauncherMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"WOWHCLauncherWnd", nullptr);
        if (existing) SetForegroundWindow(existing);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);

    g_hIconLarge = CreateIconFromPng(IDR_LOGO_ROUND, GetSystemMetrics(SM_CXICON));
    g_hIconSmall = CreateIconFromPng(IDR_LOGO_ROUND, GetSystemMetrics(SM_CXSMICON));
    if (!g_hIconLarge) g_hIconLarge = LoadIcon(nullptr, IDI_APPLICATION);
    if (!g_hIconSmall) g_hIconSmall = LoadIcon(nullptr, IDI_APPLICATION);

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarButtonCreated");

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    g_hRichEdit = LoadLibraryW(L"MSFTEDIT.DLL");

    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    g_configDir = std::wstring(appdata) + L"\\WOWHCLauncher";
    CreateDirectoryW(g_configDir.c_str(), nullptr);

    g_logPath = g_configDir + L"\\launcher.log";
    { // truncate log at each startup
        HANDLE hf = CreateFileW(g_logPath.c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf != INVALID_HANDLE_VALUE) CloseHandle(hf);
    }
    AppendLog(L"=== Launcher started (version %hs)%s%s ===", LAUNCHER_VERSION_STR,
        g_testMode ? " [--test]" : "", g_devMode ? " [--dev]" : "");
    RB_SetLogPath(g_logPath);
    if (IsDevBuild() || g_testMode)
        ShellExecuteW(nullptr, L"open", g_logPath.c_str(), nullptr, nullptr, SW_SHOW);

    LoadConfig();
    AppendLog(L"Config loaded: installPath='%s' clientPath='%s' clientType=%d hermesExePath='%s' arctiumExePath='%s' wowTweakedExePath='%s' realmIndex=%d",
        g_installPath.c_str(), g_clientPath.c_str(), (int)g_clientType,
        g_hermesExePath.c_str(), g_arctiumExePath.c_str(), g_wowTweakedExePath.c_str(), g_realmIndex);
    RB_SetSettings(LoadReplaySettings(ConfigPath()));
    EnsureDesktopShortcut(true); // refresh shortcut target to current exe path if it exists

    // If client path is saved but the game exe is missing, reset so the user is
    // prompted to set up again. Covers both interrupted downloads (no marker yet)
    // and completed installs that were later moved or deleted (marker present).
    if (!g_clientPath.empty()) {
        std::wstring wowExeCheck = (g_clientType == CT_112)
            ? (g_wowTweakedExePath.empty() ? g_clientPath + L"\\_classic_era_\\Wow_tweaked.exe" : g_wowTweakedExePath)
            : g_clientPath + L"\\_classic_era_\\WowClassic.exe";
        AppendLog(L"Startup exe check: '%s'", wowExeCheck.c_str());
        if (GetFileAttributesW(wowExeCheck.c_str()) == INVALID_FILE_ATTRIBUTES) {
            bool wasInstalled = GetFileAttributesW(ClientMarker().c_str()) != INVALID_FILE_ATTRIBUTES;
            AppendLog(L"Startup exe check FAILED (wasInstalled=%d) — clearing all paths", (int)wasInstalled);
            g_installPath.clear();
            g_clientPath.clear();
            g_hermesExePath.clear();
            g_arctiumExePath.clear();
            g_wowTweakedExePath.clear();
            g_clientType = CT_UNKNOWN;
            SaveConfig();
            if (wasInstalled) {
                MessageBoxW(nullptr,
                    L"The previously configured WoW installation could not be found.\r\n"
                    L"Please select your installation folder again.",
                    L"Installation Not Found", MB_OK | MB_ICONWARNING);
            }
        } else {
            AppendLog(L"Startup exe check OK");
        }
    }
    // On a valid saved install, try to recover missing exe paths from the stored client dir
    if (!g_clientPath.empty()) {
        if (g_clientType == CT_112) {
            if (g_wowTweakedExePath.empty() || GetFileAttributesW(g_wowTweakedExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::wstring def = g_clientPath + L"\\_classic_era_\\Wow_tweaked.exe";
                if (GetFileAttributesW(def.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    AppendLog(L"Recovered wowTweakedExePath from default: '%s'", def.c_str());
                    g_wowTweakedExePath = def;
                } else {
                    std::wstring f = FindExeInTree(g_clientPath, L"Wow_tweaked.exe", 3);
                    if (!f.empty()) { AppendLog(L"Recovered wowTweakedExePath via scan: '%s'", f.c_str()); g_wowTweakedExePath = f; }
                    else AppendLog(L"Could not recover wowTweakedExePath");
                }
            }
        } else {
            if (g_hermesExePath.empty() || GetFileAttributesW(g_hermesExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::wstring f = FindExeNearby(g_clientPath, L"HermesProxy.exe");
                if (!f.empty()) { AppendLog(L"Recovered hermesExePath: '%s'", f.c_str()); g_hermesExePath = f; }
                else AppendLog(L"Could not recover hermesExePath");
            }
            if (g_arctiumExePath.empty() || GetFileAttributesW(g_arctiumExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::wstring f = FindExeNearby(g_clientPath, L"Arctium WoW Launcher.exe");
                if (!f.empty()) { AppendLog(L"Recovered arctiumExePath: '%s'", f.c_str()); g_arctiumExePath = f; }
                else AppendLog(L"Could not recover arctiumExePath");
            }
        }
    }

    // Check WebView2 runtime before creating the window
    if (!CheckWebView2Runtime()) {
        ShowWebView2InstallPrompt(nullptr);
        return 1;
    }

    // Ensure ui/ is in AppData. Downloads from GitHub if missing (standalone EXE install).
    if (!CheckAndBootstrapUiFiles(hInst))
        return 1;

    g_hbrBg  = CreateSolidBrush(CLR_BG);
    g_hbrBg2 = CreateSolidBrush(CLR_BG2);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"WOWHCLauncherWnd";
    wc.hIcon         = g_hIconLarge;
    wc.hIconSm       = g_hIconSmall;
    RegisterClassExW(&wc);

    // Borderless popup: window size == client size (no title bar, no border)
    int WND_W = MulDiv(WND_CLIENT_W, g_dpi, 96);
    int WND_H = MulDiv(WND_CLIENT_H, g_dpi, 96);
    RECT wa = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int xpos = wa.left + ((wa.right  - wa.left > WND_W) ? (wa.right  - wa.left - WND_W) / 2 : 0);
    int ypos = wa.top  + ((wa.bottom - wa.top  > WND_H) ? (wa.bottom - wa.top  - WND_H) / 2 : 0);

    // WS_CAPTION | WS_MINIMIZEBOX: required for DWM to play the native minimize/restore
    // animation. WM_NCCALCSIZE returning 0 eliminates the visible NC area so the
    // window still looks fully borderless.
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"WOWHCLauncherWnd", APP_NAME,
        WS_POPUP | WS_CAPTION | WS_MINIMIZEBOX,
        xpos, ypos, WND_W, WND_H,
        nullptr, nullptr, hInst, nullptr);

    RB_Init(g_hwnd);
    RB_ApplySettings(LoadReplaySettings(ConfigPath()));
    RB_RegisterHotkeys();

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(g_hwnd, 19, &darkMode, sizeof(darkMode));
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    // Thin DWM shadow around the borderless window
    MARGINS shadow = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(g_hwnd, &shadow);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    if (g_hbrBg)  { DeleteObject(g_hbrBg);  g_hbrBg  = nullptr; }
    if (g_hbrBg2) { DeleteObject(g_hbrBg2); g_hbrBg2 = nullptr; }
    if (g_hRichEdit) { FreeLibrary(g_hRichEdit); g_hRichEdit = nullptr; }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    CoUninitialize();
    return (int)m.wParam;
}
