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

// ── Build-time config ──────────────────────────────────────────────────────────
static constexpr wchar_t CLIENT_DOWNLOAD_URL[] =
    L"https://client.wow-hc.com/1.14.2/WOW-1.14.2.zip";
   // L"https://dl.wow-hc.com/clients/WOW-Classic-1.14.2.zip";

static constexpr wchar_t APP_NAME[]        = L"WOW-HC Launcher";
static constexpr wchar_t HERMES_GH_OWNER[] = L"Novivy";
static constexpr wchar_t HERMES_GH_REPO[]  = L"HermesProxy";
static constexpr wchar_t ADDON_GH_OWNER[]      = L"Novivy";
static constexpr wchar_t ADDON_GH_REPO[]       = L"wow-hc-addon";
static constexpr wchar_t LAUNCHER_GH_OWNER[]   = L"Novivy";
static constexpr wchar_t LAUNCHER_GH_REPO[]    = L"wowhc-launcher";
static constexpr char    LAUNCHER_EXE_ASSET[]  = "WOW-HC-Launcher.exe";

#ifndef LAUNCHER_VERSION_STR
#define LAUNCHER_VERSION_STR "v0.0.0-dev"
#endif

// ── Resource IDs ───────────────────────────────────────────────────────────────
static const int IDR_LOGO_CLEAN = 201;
static const int IDR_LOGO_ROUND = 202;

// ── Dark-mode palette ──────────────────────────────────────────────────────────
static const COLORREF CLR_BG     = RGB(22,  22,  26);
static const COLORREF CLR_BG2    = RGB(38,  38,  44);
static const COLORREF CLR_TEXT   = RGB(210, 210, 215);
static const COLORREF CLR_ACCENT = RGB(185, 140, 25);  // kept for potential future use
static const COLORREF CLR_BAR    = RGB(30,  100, 210);
static const COLORREF CLR_SEP    = RGB(55,  55,  62);

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
    ID_TIMER_UPDATE  = 200,
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

enum UpdateComponent : WPARAM { UC_HERMES = 0, UC_ADDON = 1, UC_LAUNCHER = 2 };

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
    L"Ready to play!",
    L"Error - check your connection or installation path.",
    L"Launching...",
    L"Select a new or existing installation folder",
};

// ── Globals ────────────────────────────────────────────────────────────────────
static HINSTANCE         g_hInst             = nullptr;
static HWND              g_hwnd              = nullptr;
static HWND              g_hwndStatus        = nullptr;
static HWND              g_hwndProgress      = nullptr;
static HWND              g_hwndPath          = nullptr;
static HWND              g_hwndPlay          = nullptr;
static HWND              g_hwndBrowse        = nullptr;
static HWND              g_hwndOpen          = nullptr;
static HWND              g_hwndTransfer      = nullptr;

static HFONT             g_fontNormal        = nullptr;
static HFONT             g_fontPlay          = nullptr;
static HFONT             g_fontLink          = nullptr;
static HFONT             g_fontSmall         = nullptr;
static HWND              g_hwndLink          = nullptr;
static HWND              g_hwndLinkAddons    = nullptr;
static HWND              g_hwndVerLauncher   = nullptr;
static HWND              g_hwndVerHermes     = nullptr;
static HWND              g_hwndVerAddon      = nullptr;

static Gdiplus::Bitmap*  g_logoBitmap        = nullptr;
static ITaskbarList3*    g_pTaskbar          = nullptr;
static ULONG_PTR         g_gdiplusToken      = 0;
static UINT              g_taskbarCreatedMsg = 0;
static HICON             g_hIconLarge        = nullptr;
static HICON             g_hIconSmall        = nullptr;
static HBRUSH            g_hbrBg             = nullptr;
static HBRUSH            g_hbrBg2            = nullptr;

static std::wstring g_installPath;
static std::wstring g_clientPath;
static std::wstring g_hermesExePath;    // full path to HermesProxy.exe
static std::wstring g_arctiumExePath;   // full path to "Arctium WoW Launcher.exe"
static std::wstring g_configDir;

static std::atomic<bool> g_workerBusy{false};
static std::atomic<bool> g_playReady{false};
static std::atomic<bool> g_clientInstalled{false};

// Taskbar progress cache — restored when TaskbarButtonCreated fires after window show
static int  g_taskbarLastPct     = 0;
static bool g_taskbarHasProgress = false;

static UINT g_dpi = 96;

static bool g_playHover     = false;
static bool g_browseHover   = false;
static bool g_openHover     = false;
static bool g_transferHover = false;
static bool g_linkHover     = false;
static bool g_addonsHover   = false;
static bool g_freshInstall  = false;

// ── Config helpers ─────────────────────────────────────────────────────────────
static std::wstring ConfigPath()     { return g_configDir + L"\\launcher.ini"; }
static std::wstring HermesVerPath()  { return g_clientPath + L"\\hermes_version.txt"; }
static std::wstring AddonVerPath()   { return g_clientPath + L"\\wow_hc_addon_version.txt"; }
static std::wstring ClientMarker()   { return g_clientPath + L"\\.launcher_installed"; }

static void SaveConfig()
{
    std::wstring iniPath = ConfigPath();
    const wchar_t* ini = iniPath.c_str();
    WritePrivateProfileStringW(L"Launcher", L"InstallPath",  g_installPath.c_str(),    ini);
    WritePrivateProfileStringW(L"Launcher", L"ClientPath",   g_clientPath.c_str(),     ini);
    WritePrivateProfileStringW(L"Launcher", L"HermesExePath", g_hermesExePath.c_str(), ini);
    WritePrivateProfileStringW(L"Launcher", L"ArticulumExePath", g_arctiumExePath.c_str(), ini);
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
    // Backward compat: derive ClientPath from old InstallPath-only configs
    if (g_clientPath.empty() && !g_installPath.empty())
        g_clientPath = g_installPath + L"\\client";
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

static std::wstring ReadLocalHermesVersion()
{
    if (g_clientPath.empty()) return {};
    std::wifstream f(HermesVerPath());
    if (!f.is_open()) return {};
    std::wstring v;
    std::getline(f, v);
    while (!v.empty() && (v.back() == L'\r' || v.back() == L'\n' || v.back() == L' '))
        v.pop_back();
    return v;
}

static void WriteLocalHermesVersion(const std::wstring& ver)
{
    std::wofstream f(HermesVerPath());
    f << ver;
}

// EXE VERSIONINFO is ground truth; .txt is fallback for exes without a resource
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
    if (v.empty()) v = ReadLocalHermesVersion();
    return v;
}

static std::wstring ReadLocalAddonVersion()
{
    if (g_clientPath.empty()) return {};
    std::wifstream f(AddonVerPath());
    if (!f.is_open()) return {};
    std::wstring v;
    std::getline(f, v);
    while (!v.empty() && (v.back() == L'\r' || v.back() == L'\n' || v.back() == L' '))
        v.pop_back();
    return v;
}

static void WriteLocalAddonVersion(const std::wstring& ver)
{
    std::wofstream f(AddonVerPath());
    f << ver;
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

static std::string FindAssetUrl(const std::string& json)
{
    std::string fallback;
    size_t pos = 0;
    while (true) {
        size_t found = json.find("browser_download_url", pos);
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
    if (!hSess) return {};
    HINTERNET hConn = nullptr;
    HINTERNET hReq  = MakeRequest(hSess, url, hConn);
    if (!hReq) { WinHttpCloseHandle(hSess); return {}; }
    WinHttpAddRequestHeaders(hReq,
        L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    std::string body;
    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr))
    {
        DWORD avail = 0, read = 0;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            std::vector<char> buf(avail + 1);
            WinHttpReadData(hReq, buf.data(), avail, &read);
            body.append(buf.data(), read);
        }
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return body;
}

static bool HttpDownload(const std::wstring& url, const std::wstring& dest,
    const std::function<void(DWORD64, DWORD64)>& progress = nullptr)
{
    HINTERNET hSess = OpenSession();
    if (!hSess) return false;
    HINTERNET hConn = nullptr;
    HINTERNET hReq  = MakeRequest(hSess, url, hConn);
    if (!hReq) { WinHttpCloseHandle(hSess); return false; }
    bool ok = false;
    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr))
    {
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
            MessageBoxW(nullptr,
                (L"HTTP error " + std::to_wstring(statusCode) +
                 L"\nCheck: " + logPath).c_str(),
                L"WOW-HC Debug", MB_OK | MB_ICONWARNING);
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

static void LaunchExe(const std::wstring& exe, const std::wstring& args)
{
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!args.empty()) { cmd += L' '; cmd += args; }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    std::wstring dir = exe.substr(0, exe.rfind(L'\\'));
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0,
            nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

static void LaunchHermes(const std::wstring& exe)
{
    LaunchExe(exe, L"");
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

// ── UI helpers ─────────────────────────────────────────────────────────────────
static void PostStatus(WorkerStatus s)
{
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

static void RefreshTransferButton()
{
    if (!g_hwndTransfer) return;
    bool enabled = g_clientInstalled.load() && !g_workerBusy.load() && !g_clientPath.empty();
    EnableWindow(g_hwndTransfer, enabled ? TRUE : FALSE);
}

static void RefreshPlayButton()
{
    bool installed = g_clientInstalled.load();
    bool ready     = g_playReady.load();
    bool hasPath   = !g_clientPath.empty();
    bool busy      = g_workerBusy.load();
    SetWindowTextW(g_hwndPlay, installed ? L"PLAY" : L"INSTALL");
    bool enable = hasPath && (installed ? ready : !busy);
    EnableWindow(g_hwndPlay, enable ? TRUE : FALSE);
    if (g_hwndBrowse) EnableWindow(g_hwndBrowse, busy ? FALSE : TRUE);
    if (g_hwndOpen)   EnableWindow(g_hwndOpen, (hasPath && !busy) ? TRUE : FALSE);
    RefreshTransferButton();
}

static void SetVerLabel(HWND ctrl, const std::wstring& text)
{
    // Transparent static controls don't erase old text on update.
    // Invalidate the parent's region under the control so WM_ERASEBKGND
    // repaints the dark background before the control redraws.
    RECT rc; GetWindowRect(ctrl, &rc);
    MapWindowPoints(HWND_DESKTOP, g_hwnd, reinterpret_cast<POINT*>(&rc), 2);
    InvalidateRect(g_hwnd, &rc, TRUE);
    UpdateWindow(g_hwnd);
    SetWindowTextW(ctrl, text.c_str());
}

static void RefreshVersionLabels()
{
    if (!g_hwndVerLauncher) return;
    const char* lv = LAUNCHER_VERSION_STR;
    std::wstring lvW(lv, lv + strlen(lv));
    SetVerLabel(g_hwndVerLauncher, L"Launcher " + lvW);
    std::wstring hv = GetLocalHermesVersion();
    SetVerLabel(g_hwndVerHermes, L"HermesProxy " + (hv.empty() ? L"" : hv));
    std::wstring av = ReadLocalAddonVersion();
    SetVerLabel(g_hwndVerAddon, L"Addon " + (av.empty() ? L"" : av));
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

// ── EXE asset URL (for self-update) ────────────────────────────────────────────
static std::string FindExeAssetUrl(const std::string& json)
{
    size_t pos = 0;
    while (true) {
        size_t found = json.find("browser_download_url", pos);
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
            WriteLocalHermesVersion(remoteVer);
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
    std::wstring apiUrl = std::wstring(L"https://api.github.com/repos/")
        + ADDON_GH_OWNER + L"/" + ADDON_GH_REPO + L"/releases/latest";
    std::string json = HttpGet(apiUrl);
    if (json.empty()) return;

    std::string tag = JsonString(json, "tag_name");
    std::wstring remoteVer(tag.begin(), tag.end());
    std::wstring localVer  = ReadLocalAddonVersion();
    std::wstring addonPath = g_clientPath + L"\\_classic_era_\\Interface\\AddOns\\WOW_HC";
    bool folderMissing = GetFileAttributesW(addonPath.c_str()) == INVALID_FILE_ATTRIBUTES;
    if (remoteVer.empty() || (!IsNewer(localVer, remoteVer) && !folderMissing)) return;

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
        std::wstring addonsDir = g_clientPath + L"\\_classic_era_\\Interface\\AddOns";
        CreateDirectoryW((g_clientPath + L"\\_classic_era_").c_str(), nullptr);
        CreateDirectoryW((g_clientPath + L"\\_classic_era_\\Interface").c_str(), nullptr);
        CreateDirectoryW(addonsDir.c_str(), nullptr);
        std::wstring addonDest = addonsDir + L"\\WOW_HC";
        DeleteDirRecursive(addonDest);
        CreateDirectoryW(addonDest.c_str(), nullptr);
        ok = ExtractZipSmart(tmpZip, addonDest, true, [](int pct) {
            PostPct(70 + pct * 30 / 100);
        });
        DeleteFileW(tmpZip.c_str());
        if (ok) { WriteLocalAddonVersion(remoteVer); PostPct(100); }
    } else {
        DeleteFileW(tmpZip.c_str());
    }
}

// Checks for a newer launcher release, prompts, downloads and hot-swaps the EXE.
// Safe to call from any thread. Skips if a worker operation is in progress.
static void RunLauncherUpdateCheck()
{
    if (g_workerBusy.load()) return;

    // Clean up any leftover .old from a previous self-update
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    DeleteFileW((std::wstring(exePath) + L".old").c_str());

    std::wstring apiUrl = std::wstring(L"https://api.github.com/repos/")
        + LAUNCHER_GH_OWNER + L"/" + LAUNCHER_GH_REPO + L"/releases/latest";
    std::string json = HttpGet(apiUrl);
    if (json.empty()) return;

    std::string tag = JsonString(json, "tag_name");
    std::wstring remoteVer(tag.begin(), tag.end());
    std::wstring localVer = GetLauncherVersion();
    if (remoteVer.empty() || !IsNewer(localVer, remoteVer)) return;

    std::wstring payload = remoteVer + L"\n" + localVer;
    LRESULT r = SendMessageW(g_hwnd, WM_ASK_UPDATE, UC_LAUNCHER,
        (LPARAM)(new std::wstring(payload)));
    if (r != IDYES) return;

    std::string assetUrl = FindExeAssetUrl(json);
    if (assetUrl.empty()) {
        // Fallback: construct direct GitHub release download URL
        assetUrl = std::string("https://github.com/Novivy/wowhc-launcher/releases/latest/download/")
                 + LAUNCHER_EXE_ASSET;
    }

    std::wstring assetW(assetUrl.begin(), assetUrl.end());
    std::wstring tmpExe = TempFile(L"wowhc_launcher_new.exe");
    PostText(L"Downloading launcher update...");
    if (!HttpDownload(assetW, tmpExe)) {
        DeleteFileW(tmpExe.c_str());
        MessageBoxW(g_hwnd, L"Failed to download the launcher update.\nCheck your internet connection and try again.", L"Update Failed", MB_OK | MB_ICONERROR);
        return;
    }

    SendMessageW(g_hwnd, WM_APPLY_SELF_UPD, 0, (LPARAM)(new std::wstring(tmpExe)));
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
};

static bool FindWowInstall(const std::wstring& folder, WowInstallInfo& info, int depth = 2)
{
    // Case 1: folder IS _classic_era_ (WowClassic.exe directly inside)
    {
        std::wstring p = folder + L"\\WowClassic.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            info.wowExePath = p;
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
            return true;
        }
    }
    // Case 3: folder is launcher install root (contains client\_classic_era_)
    {
        std::wstring p = folder + L"\\client\\_classic_era_\\WowClassic.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            info.wowExePath = p;
            info.clientDir  = folder + L"\\client";
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
        RunHermesUpdateCheck();
        RunAddonUpdateCheck();
        WriteLastCheckTime();
        // Refresh exe paths in case an update installed/moved files
        if (g_hermesExePath.empty() || GetFileAttributesW(g_hermesExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wstring f = FindExeInTree(g_clientPath, L"HermesProxy.exe", 2);
            if (!f.empty()) { g_hermesExePath = f; SaveConfig(); }
        }
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
        if (!clientOk) {
            std::wstring arctium = g_clientPath + L"\\Arctium WoW Launcher.exe";
            if (GetFileAttributesW(arctium.c_str()) != INVALID_FILE_ATTRIBUTES)
                clientOk = true;
        }
        if (!clientOk) {
            std::wstring wowExe = g_clientPath + L"\\_classic_era_\\WowClassic.exe";
            if (GetFileAttributesW(wowExe.c_str()) != INVALID_FILE_ATTRIBUTES)
                clientOk = true;
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
        bool ok = HttpDownload(CLIENT_DOWNLOAD_URL, tmpZip,
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

        g_clientInstalled = true;
        g_freshInstall    = true;
        PostMessageW(g_hwnd, WM_SET_INSTALL_MODE, 1, 0);
        PostPct(100);
    }

    // ── 2+3. Check HermesProxy and addon updates on every startup ──────────────────────────
    PostStatus(WS_CHECKING);
    PostPct(0);
    RunHermesUpdateCheck();
    RunAddonUpdateCheck();
    WriteLastCheckTime();

    // After Hermes update/install: refresh Arctium path for new installs if it wasn't found yet
    if (g_arctiumExePath.empty() || GetFileAttributesW(g_arctiumExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring found = FindExeInTree(g_clientPath, L"Arctium WoW Launcher.exe", 2);
        if (!found.empty()) g_arctiumExePath = found;
    }
    if (g_hermesExePath.empty() || GetFileAttributesW(g_hermesExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring found = FindExeInTree(g_clientPath, L"HermesProxy.exe", 2);
        if (!found.empty()) g_hermesExePath = found;
    }
    SaveConfig();

    // ── 4. Verify all required executables are present ───────────────────────────
    g_workerBusy = false;
    bool hermesOk  = !g_hermesExePath.empty()  &&
        GetFileAttributesW(g_hermesExePath.c_str())  != INVALID_FILE_ATTRIBUTES;
    bool arctiumOk = !g_arctiumExePath.empty() &&
        GetFileAttributesW(g_arctiumExePath.c_str()) != INVALID_FILE_ATTRIBUTES;
    bool addonOk   = GetFileAttributesW(
        (g_clientPath + L"\\_classic_era_\\Interface\\AddOns\\WOW_HC").c_str()) != INVALID_FILE_ATTRIBUTES;

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
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (msg == WM_MOUSELEAVE) {
        *pHover = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void EnsureDesktopShortcut(bool onlyIfAlreadyExists = false);

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
        return 0;
    }

    switch (msg)
    {
    case WM_CREATE:
    {
        auto D = [](int px) -> int { return MulDiv(px, g_dpi, 96); };

        auto MakeFont = [](int ptSize, int weight) -> HFONT {
            LOGFONTW lf = {};
            lf.lfHeight  = -MulDiv(ptSize, g_dpi, 72);
            lf.lfWeight  = weight;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            return CreateFontIndirectW(&lf);
        };
        g_fontNormal = MakeFont(9,  FW_NORMAL);
        g_fontPlay   = MakeFont(13, FW_BOLD);
        g_fontSmall  = MakeFont(8,  FW_NORMAL);

        {
            LOGFONTW lf = {};
            lf.lfHeight    = -MulDiv(9, g_dpi, 72);
            lf.lfWeight    = FW_NORMAL;
            lf.lfUnderline = TRUE;
            lf.lfCharSet   = DEFAULT_CHARSET;
            lf.lfQuality   = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            g_fontLink = CreateFontIndirectW(&lf);
        }

        auto SF = [](HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); };

        g_hwndLink = CreateWindowExW(0, L"STATIC", L"wow-hc.com",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
            D(150), D(150), D(200), D(16), hwnd,
            (HMENU)(UINT_PTR)ID_LINK_WEBSITE, nullptr, nullptr);
        SF(g_hwndLink, g_fontLink);
        SetWindowSubclass(g_hwndLink, BtnSubclassProc, 10, (DWORD_PTR)&g_linkHover);

        g_hwndStatus = CreateWindowExW(0, L"STATIC", STATUS_TEXT[WS_NO_PATH],
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            D(10), D(196), D(480), D(16), hwnd,
            (HMENU)(UINT_PTR)ID_STATIC_STATUS, nullptr, nullptr);
        SF(g_hwndStatus, g_fontNormal);

        g_hwndProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            D(10), D(215), D(480), D(14), hwnd,
            (HMENU)(UINT_PTR)ID_PROGRESS, nullptr, nullptr);
        SendMessageW(g_hwndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(g_hwndProgress, PBM_SETBKCOLOR,  0, (LPARAM)CLR_BG2);
        SendMessageW(g_hwndProgress, PBM_SETBARCOLOR,  0, (LPARAM)CLR_BAR);

        HWND hLabel = CreateWindowExW(0, L"STATIC", L"Installation path:",
            WS_CHILD | WS_VISIBLE,
            D(10), D(250), D(480), D(16), hwnd, nullptr, nullptr, nullptr);
        SF(hLabel, g_fontNormal);

        g_hwndPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            g_installPath.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
            D(10), D(268), D(305), D(22), hwnd,
            (HMENU)(UINT_PTR)ID_EDIT_PATH, nullptr, nullptr);
        SF(g_hwndPath, g_fontNormal);

        g_hwndBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            D(324), D(268), D(82), D(22), hwnd,
            (HMENU)(UINT_PTR)ID_BTN_BROWSE, nullptr, nullptr);
        SF(g_hwndBrowse, g_fontNormal);

        g_hwndOpen = CreateWindowExW(0, L"BUTTON", L"Open",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
            D(410), D(268), D(82), D(22), hwnd,
            (HMENU)(UINT_PTR)ID_BTN_OPEN, nullptr, nullptr);
        SF(g_hwndOpen, g_fontNormal);

        // Play/Install — centered, taller primary action button
        g_hwndPlay = CreateWindowExW(0, L"BUTTON", L"INSTALL",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
            D(152), D(320), D(195), D(68), hwnd,
            (HMENU)(UINT_PTR)ID_BTN_PLAY, nullptr, nullptr);
        SF(g_hwndPlay, g_fontPlay);

        // Transfer — smaller secondary button below Play
        g_hwndTransfer = CreateWindowExW(0, L"BUTTON",
            L"Transfer UI/Macros/Addons/Settings from existing installation",
            WS_CHILD | BS_OWNERDRAW | WS_DISABLED,
            D(60), D(409), D(400), D(22), hwnd,
            (HMENU)(UINT_PTR)ID_BTN_TRANSFER, nullptr, nullptr);
        SF(g_hwndTransfer, g_fontNormal);

        g_hwndLinkAddons = CreateWindowExW(0, L"STATIC", L"Get more Addons",
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOTIFY,
            D(290), D(385), D(200), D(14), hwnd,
            (HMENU)(UINT_PTR)ID_LINK_ADDONS, nullptr, nullptr);
        SF(g_hwndLinkAddons, g_fontLink);

        g_hwndVerAddon = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            D(10), D(365), D(220), D(13), hwnd, nullptr, nullptr, nullptr);
        SF(g_hwndVerAddon, g_fontSmall);

        g_hwndVerHermes = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            D(10), D(375), D(220), D(13), hwnd, nullptr, nullptr, nullptr);
        SF(g_hwndVerHermes, g_fontSmall);

        g_hwndVerLauncher = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            D(10), D(385), D(220), D(13), hwnd, nullptr, nullptr, nullptr);
        SF(g_hwndVerLauncher, g_fontSmall);






        RefreshVersionLabels();

        SetWindowPos(g_hwndPlay, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        SetWindowSubclass(g_hwndPlay,     BtnSubclassProc, 0, (DWORD_PTR)&g_playHover);
        SetWindowSubclass(g_hwndBrowse,   BtnSubclassProc, 1, (DWORD_PTR)&g_browseHover);
        SetWindowSubclass(g_hwndOpen,     BtnSubclassProc, 3, (DWORD_PTR)&g_openHover);
        SetWindowSubclass(g_hwndTransfer, BtnSubclassProc, 2, (DWORD_PTR)&g_transferHover);
        SetWindowSubclass(g_hwndLinkAddons, BtnSubclassProc, 11, (DWORD_PTR)&g_addonsHover);

        SetTimer(hwnd, ID_TIMER_UPDATE, 24u * 60u * 60u * 1000u, nullptr);

        // Always check for a launcher update first; nothing else runs until it finishes.
        EnableWindow(g_hwndBrowse,   FALSE);
        EnableWindow(g_hwndPlay,     FALSE);
        EnableWindow(g_hwndOpen,     FALSE);
        EnableWindow(g_hwndTransfer, FALSE);
        SetWindowTextW(g_hwndStatus, L"Checking for launcher update...");
        std::thread([]() {
            RunLauncherUpdateCheck();
            PostMessageW(g_hwnd, WM_STARTUP_CHECK_DONE, 0, 0);
        }).detach();

        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        {
            HPEN hpen    = CreatePen(PS_SOLID, 1, CLR_SEP);
            HPEN hpenOld = (HPEN)SelectObject(hdc, hpen);
            int  sepY    = MulDiv(242, g_dpi, 96);
            MoveToEx(hdc, MulDiv(10,  g_dpi, 96), sepY, nullptr);
            LineTo  (hdc, MulDiv(510, g_dpi, 96), sepY);
            SelectObject(hdc, hpenOld);
            DeleteObject(hpen);
        }

        if (g_logoBitmap) {
            UINT imgW = g_logoBitmap->GetWidth();
            UINT imgH = g_logoBitmap->GetHeight();
            if (imgH == 0) imgH = 1;
            int areaX = MulDiv(100,  g_dpi, 96);
            int areaY = MulDiv(30,   g_dpi, 96);
            int areaW = MulDiv(300, g_dpi, 96);
            int areaH = MulDiv(112, g_dpi, 96);
            int drawW, drawH;
            if ((UINT64)imgW * areaH > (UINT64)imgH * areaW) {
                drawW = areaW;
                drawH = MulDiv(areaW, (int)imgH, (int)imgW);
            } else {
                drawH = areaH;
                drawW = MulDiv(areaH, (int)imgW, (int)imgH);
            }
            int drawX = areaX + (areaW - drawW) / 2;
            int drawY = areaY + (areaH - drawH) / 2;
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(g_logoBitmap, drawX, drawY, drawW, drawH);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

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

        if (id == ID_BTN_OPEN) {
            std::wstring openPath = g_clientPath + L"\\_classic_era_";
            ShellExecuteW(nullptr, L"explore", openPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }

        if (id == ID_BTN_BROWSE) {
            IFileOpenDialog* pDlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg)))) {
                DWORD opts = 0; pDlg->GetOptions(&opts);
                pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                pDlg->SetTitle(L"Select your WoW Classic 1.14.2 folder, or an empty folder for a new installation");
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
                                int verMaj=0, verMin=0, verPatch=0;
                                bool gotVer = GetExeVersion(info.wowExePath, verMaj, verMin, verPatch);
                                bool versionOk = gotVer && (verMaj == 1 && verMin == 14 && verPatch == 2);

                                if (gotVer && !versionOk) {
                                    wchar_t msg[320];
                                    swprintf_s(msg,
                                        L"This WoW installation (version %d.%d.%d) is not compatible.\r\n\r\n"
                                        L"Only WoW Classic Era 1.14.2 is supported.\r\n"
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
                                    // Search for HermesProxy.exe and Arctium in/near clientDir
                                    std::wstring foundHermes  = FindExeNearby(info.clientDir, L"HermesProxy.exe");
                                    std::wstring foundArctium = FindExeNearby(info.clientDir, L"Arctium WoW Launcher.exe");

                                    if (foundHermes.empty() || foundArctium.empty()) {
                                        // Build list of what's missing
                                        std::wstring missing;
                                        if (foundHermes.empty())  missing += L"  - HermesProxy.exe\r\n";
                                        if (foundArctium.empty()) missing += L"  - Arctium WoW Launcher.exe\r\n";
                                        std::wstring mbMsg =
                                            std::wstring(L"The following file(s) were not found in or near the selected folder:\r\n\r\n")
                                            + missing
                                            + L"\r\nIf they are installed in a parent folder, please select that folder instead.\r\n"
                                              L"Otherwise choose an empty folder and the launcher will install everything.";
                                        MessageBoxW(hwnd, mbMsg.c_str(), L"Missing Files", MB_OK | MB_ICONWARNING);
                                    } else if (g_clientPath != info.clientDir) {
                                        g_clientPath     = info.clientDir;
                                        g_installPath    = info.clientDir;
                                        g_hermesExePath  = foundHermes;
                                        g_arctiumExePath = foundArctium;
                                        SetWindowTextW(g_hwndPath, g_installPath.c_str());
                                        ResetLastCheckTime();
                                        SaveConfig();
                                        if (!g_workerBusy.load()) {
                                            g_workerBusy = true;
                                            std::thread(Worker).detach();
                                        }
                                        RefreshPlayButton();
                                    }
                                }
                            } else {
                                // ── No WoW found — new installation ─────────────
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
                                        g_installPath    = effectivePath;
                                        g_clientPath     = effectivePath + L"\\client";
                                        g_hermesExePath  = g_clientPath + L"\\HermesProxy.exe";
                                        g_arctiumExePath = g_clientPath + L"\\Arctium WoW Launcher.exe";
                                        SetWindowTextW(g_hwndPath, g_installPath.c_str());
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
                EnableWindow(g_hwndPlay, FALSE);
                PostStatus(WS_LAUNCHING);
                std::wstring hermesExe  = g_hermesExePath;
                std::wstring arctiumExe = g_arctiumExePath;
                std::thread([hermesExe, arctiumExe]() {

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
                    LaunchHermes(hermesExe);
                    Sleep(1500);
                    LaunchExe(arctiumExe, L"--staticseed --version=ClassicEra");
                    Sleep(2000);
                    PostMessageW(g_hwnd, WM_WORKER_DONE, 1, 0);
                }).detach();
            }
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

    case WM_WORKER_STATUS:
    {
        int s = (int)wp;
        if (s >= 0 && s <= WS_NO_PATH)
            SetWindowTextW(g_hwndStatus, STATUS_TEXT[s]);
        break;
    }

    case WM_WORKER_TEXT:
    {
        auto* str = reinterpret_cast<std::wstring*>(lp);
        SetWindowTextW(g_hwndStatus, str->c_str());
        delete str;
        break;
    }

    case WM_WORKER_PROGRESS:
    {
        SendMessageW(g_hwndProgress, PBM_SETPOS, wp, 0);
        g_taskbarLastPct = (int)wp;
        if (g_workerBusy.load()) {
            g_taskbarHasProgress = true;
            if (g_pTaskbar) {
                g_pTaskbar->SetProgressState(g_hwnd, TBPF_NORMAL);
                g_pTaskbar->SetProgressValue(g_hwnd, wp, 100);
            }
        }
        break;
    }

    case WM_WORKER_DONE:
    {
        bool ok = (wp != 0) && !g_clientPath.empty();
        g_playReady = ok;
        if (ok) g_clientInstalled = true;
        g_taskbarHasProgress = false;
        RefreshPlayButton();
        if (ok) PostStatus(WS_READY);
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
        RefreshPlayButton();
        break;
    }

    case WM_TIMER:
        if (wp == ID_TIMER_UPDATE && !g_workerBusy.load())
            std::thread(PeriodicUpdateCheck).detach();
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
        // Startup launcher-update check is done (update was applied+relaunched, or skipped).
        // Now re-enable UI and proceed with the normal startup flow.
        EnableWindow(g_hwndBrowse,   TRUE);
        EnableWindow(g_hwndOpen,     !g_installPath.empty());
        EnableWindow(g_hwndTransfer, TRUE);
        if (!g_clientPath.empty()) {
            g_workerBusy = true;
            RefreshPlayButton();
            std::thread(Worker).detach();
        } else {
            PostStatus(WS_NO_PATH);
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_BROWSE, BN_CLICKED), 0);
        }
        return 0;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis->hwndItem != g_hwndPlay &&
            dis->hwndItem != g_hwndBrowse &&
            dis->hwndItem != g_hwndOpen &&
            dis->hwndItem != g_hwndTransfer)
            break;

        RECT rc       = dis->rcItem;
        HDC  hdc      = dis->hDC;
        bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
        bool disabled = (dis->itemState & ODS_DISABLED)  != 0;
        bool isPlay   = (dis->hwndItem == g_hwndPlay);
        bool hover    = !disabled && (isPlay ? g_playHover :
                        (dis->hwndItem == g_hwndBrowse) ? g_browseHover :
                        (dis->hwndItem == g_hwndOpen)   ? g_openHover : g_transferHover);

        COLORREF bg;
        if (pressed)       bg = RGB(55, 55, 62);
        else if (hover)    bg = isPlay ? RGB(68, 58, 30) : RGB(58, 58, 66);
        else               bg = RGB(45, 45, 52);
        COLORREF fg = disabled ? RGB(90,90,95) : CLR_TEXT;

        HBRUSH hbr = CreateSolidBrush(bg);
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);

        COLORREF borderClr = hover ? (isPlay ? RGB(140,105,20) : RGB(100,100,110)) : RGB(80,80,88);
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
        SetTextColor(hdc, fg);
        SelectObject(hdc, isPlay ? g_fontPlay : g_fontNormal);

        wchar_t txt[128] = {};
        GetWindowTextW(dis->hwndItem, txt, 128);
        DrawTextW(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (dis->itemState & ODS_FOCUS)
            DrawFocusRect(hdc, &rc);

        return TRUE;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, CLR_BG2);
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)g_hbrBg2;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC  hdc     = (HDC)wp;
        HWND hCtl    = (HWND)lp;
        SetBkColor(hdc, CLR_BG);
        if (hCtl == g_hwndLink) {
            COLORREF linkClr = g_linkHover ? RGB(140, 200, 255) : RGB(100, 170, 240);
            SetTextColor(hdc, linkClr);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)g_hbrBg;
        }
        if (hCtl == g_hwndLinkAddons) {
            COLORREF linkClr = g_addonsHover ? RGB(140, 200, 255) : RGB(100, 170, 240);
            SetTextColor(hdc, linkClr);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (hCtl == g_hwndVerLauncher || hCtl == g_hwndVerHermes || hCtl == g_hwndVerAddon) {
            SetTextColor(hdc, RGB(100, 100, 110));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)g_hbrBg;
    }

    case WM_ERASEBKGND:
    {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_hbrBg);
        return 1;
    }

    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        LONG w = MulDiv(514, g_dpi, 96);
        LONG h = MulDiv(440, g_dpi, 96);
        mmi->ptMinTrackSize = {w, h};
        mmi->ptMaxTrackSize = {w, h};
        break;
    }

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_UPDATE);
        if (g_pTaskbar) { g_pTaskbar->Release(); g_pTaskbar = nullptr; }
        if (g_hIconLarge) { DestroyIcon(g_hIconLarge); g_hIconLarge = nullptr; }
        if (g_hIconSmall) { DestroyIcon(g_hIconSmall); g_hIconSmall = nullptr; }
        if (g_fontLink)   { DeleteObject(g_fontLink);  g_fontLink   = nullptr; }
        if (g_fontSmall)  { DeleteObject(g_fontSmall); g_fontSmall  = nullptr; }
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

// ── Entry point ────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInst;

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

    g_logoBitmap = LoadPngFromResource(IDR_LOGO_CLEAN);

    g_hIconLarge = CreateIconFromPng(IDR_LOGO_ROUND, GetSystemMetrics(SM_CXICON));
    g_hIconSmall = CreateIconFromPng(IDR_LOGO_ROUND, GetSystemMetrics(SM_CXSMICON));
    if (!g_hIconLarge) g_hIconLarge = LoadIcon(nullptr, IDI_APPLICATION);
    if (!g_hIconSmall) g_hIconSmall = LoadIcon(nullptr, IDI_APPLICATION);

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarButtonCreated");

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    g_configDir = std::wstring(appdata) + L"\\WOWHCLauncher";
    CreateDirectoryW(g_configDir.c_str(), nullptr);
    LoadConfig();
    EnsureDesktopShortcut(true); // refresh shortcut target to current exe path if it exists

    // If a fully-installed client is saved but WowClassic.exe is gone, reset paths so
    // the user is prompted to choose again (moved/deleted installation).
    if (!g_clientPath.empty() &&
        GetFileAttributesW(ClientMarker().c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring wowExe = g_clientPath + L"\\_classic_era_\\WowClassic.exe";
        if (GetFileAttributesW(wowExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
            g_installPath.clear();
            g_clientPath.clear();
            g_hermesExePath.clear();
            g_arctiumExePath.clear();
            SaveConfig();
            MessageBoxW(nullptr,
                L"The previously configured WoW installation could not be found.\r\n"
                L"Please select your installation folder again.",
                L"Installation Not Found", MB_OK | MB_ICONWARNING);
        }
    }
    // On a valid saved install, try to recover missing exe paths from the stored client dir
    if (!g_clientPath.empty()) {
        if (g_hermesExePath.empty() || GetFileAttributesW(g_hermesExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wstring f = FindExeNearby(g_clientPath, L"HermesProxy.exe");
            if (!f.empty()) g_hermesExePath = f;
        }
        if (g_arctiumExePath.empty() || GetFileAttributesW(g_arctiumExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wstring f = FindExeNearby(g_clientPath, L"Arctium WoW Launcher.exe");
            if (!f.empty()) g_arctiumExePath = f;
        }
    }

    g_hbrBg  = CreateSolidBrush(CLR_BG);
    g_hbrBg2 = CreateSolidBrush(CLR_BG2);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_hbrBg;
    wc.lpszClassName = L"WOWHCLauncherWnd";
    wc.hIcon         = g_hIconLarge;
    wc.hIconSm       = g_hIconSmall;
    RegisterClassExW(&wc);

    int WND_W = MulDiv(514, g_dpi, 96);
    int WND_H = MulDiv(440, g_dpi, 96);
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowExW(0, L"WOWHCLauncherWnd", APP_NAME,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (sx - WND_W) / 2, (sy - WND_H) / 2, WND_W, WND_H,
        nullptr, nullptr, hInst, nullptr);

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    delete g_logoBitmap;
    g_logoBitmap = nullptr;
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);

    if (g_hbrBg)  { DeleteObject(g_hbrBg);  g_hbrBg  = nullptr; }
    if (g_hbrBg2) { DeleteObject(g_hbrBg2); g_hbrBg2 = nullptr; }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    CoUninitialize();
    return (int)m.wParam;
}
