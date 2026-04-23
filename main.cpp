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

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "version.lib")

// ── Build-time config ──────────────────────────────────────────────────────────
static constexpr wchar_t CLIENT_DOWNLOAD_URL[] =
    L"https://dl.wow-hc.com/clients/WOW-Classic-1.14.2.zip";

static constexpr wchar_t APP_NAME[]        = L"WOW-HC.com";
static constexpr wchar_t HERMES_GH_OWNER[] = L"Novivy";
static constexpr wchar_t HERMES_GH_REPO[]  = L"HermesProxy";
static constexpr wchar_t ADDON_GH_OWNER[]  = L"Novivy";
static constexpr wchar_t ADDON_GH_REPO[]   = L"wow-hc-addon";

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
};

#define WM_WORKER_STATUS    (WM_APP + 1)  // wParam = WorkerStatus
#define WM_WORKER_PROGRESS  (WM_APP + 2)  // wParam = 0-100
#define WM_WORKER_DONE      (WM_APP + 3)  // wParam = 1 success / 0 fail
#define WM_WORKER_TEXT      (WM_APP + 4)  // lParam = new std::wstring*
#define WM_SET_INSTALL_MODE (WM_APP + 5)  // wParam = 0 not installed / 1 installed
#define WM_TRANSFER_DONE    (WM_APP + 6)  // wParam = 1 success / 0 fail

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
    L"Downloading WoW client...",
    L"Installing WoW client...",
    L"Downloading WOW_HC addon...",
    L"Installing WOW_HC addon...",
    L"Transferring UI, macros and addons...",
    L"Ready to play!",
    L"Error - check your connection or installation path.",
    L"Launching...",
    L"Select an installation folder, then click Browse.",
};

// ── Globals ────────────────────────────────────────────────────────────────────
static HINSTANCE         g_hInst             = nullptr;
static HWND              g_hwnd              = nullptr;
static HWND              g_hwndStatus        = nullptr;
static HWND              g_hwndProgress      = nullptr;
static HWND              g_hwndPath          = nullptr;
static HWND              g_hwndPlay          = nullptr;
static HWND              g_hwndBrowse        = nullptr;
static HWND              g_hwndTransfer      = nullptr;

static HFONT             g_fontNormal        = nullptr;
static HFONT             g_fontPlay          = nullptr;
static HFONT             g_fontLink          = nullptr;
static HWND              g_hwndLink          = nullptr;

static Gdiplus::Bitmap*  g_logoBitmap        = nullptr;
static ITaskbarList3*    g_pTaskbar          = nullptr;
static ULONG_PTR         g_gdiplusToken      = 0;
static UINT              g_taskbarCreatedMsg = 0;
static HICON             g_hIconLarge        = nullptr;
static HICON             g_hIconSmall        = nullptr;
static HBRUSH            g_hbrBg             = nullptr;
static HBRUSH            g_hbrBg2            = nullptr;

static std::wstring g_installPath;
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
static bool g_transferHover = false;
static bool g_linkHover     = false;
static bool g_freshInstall  = false;

// ── Config helpers ─────────────────────────────────────────────────────────────
static std::wstring ConfigPath()     { return g_configDir + L"\\launcher.ini"; }
static std::wstring HermesVerPath()  { return g_installPath + L"\\client\\hermes_version.txt"; }
static std::wstring AddonVerPath()   { return g_installPath + L"\\client\\wow_hc_addon_version.txt"; }
static std::wstring ClientMarker()   { return g_installPath + L"\\client\\.launcher_installed"; }

static void SaveConfig()
{
    WritePrivateProfileStringW(L"Launcher", L"InstallPath",
        g_installPath.c_str(), ConfigPath().c_str());
}

static void LoadConfig()
{
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Launcher", L"InstallPath", L"",
        buf, MAX_PATH, ConfigPath().c_str());
    g_installPath = buf;
}

static std::wstring ReadLocalHermesVersion()
{
    if (g_installPath.empty()) return {};
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

static std::wstring ReadLocalAddonVersion()
{
    if (g_installPath.empty()) return {};
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
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return ok;
}

// ── Temp path ──────────────────────────────────────────────────────────────────
static std::wstring TempFile(const std::wstring& name)
{
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + name;
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
    bool enabled = g_clientInstalled.load() && !g_workerBusy.load() && !g_installPath.empty();
    EnableWindow(g_hwndTransfer, enabled ? TRUE : FALSE);
}

static void RefreshPlayButton()
{
    bool installed = g_clientInstalled.load();
    bool ready     = g_playReady.load();
    bool hasPath   = !g_installPath.empty();
    bool busy      = g_workerBusy.load();
    SetWindowTextW(g_hwndPlay, installed ? L"PLAY" : L"INSTALL");
    bool enable = hasPath && (installed ? ready : !busy);
    EnableWindow(g_hwndPlay, enable ? TRUE : FALSE);
    RefreshTransferButton();
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

// ── Worker thread ──────────────────────────────────────────────────────────────
static void Worker()
{
    g_workerBusy = true;
    g_playReady  = false;

    // ── 1. Check client installation ──────────────────────────────────────────
    bool clientOk = false;
    if (!g_installPath.empty()) {
        clientOk = GetFileAttributesW(ClientMarker().c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!clientOk) {
            std::wstring exe = g_installPath + L"\\client\\Arctium WoW Launcher.exe";
            clientOk = GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES;
            if (clientOk) {
                HANDLE h = CreateFileW(ClientMarker().c_str(), GENERIC_WRITE, 0,
                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            }
        }
    }

    g_clientInstalled = clientOk;
    PostMessageW(g_hwnd, WM_SET_INSTALL_MODE, clientOk ? 1 : 0, 0);

    if (!clientOk) {
        PostStatus(WS_DL_CLIENT);
        PostPct(0);

        std::wstring tmpZip = TempFile(L"wowclient_dl.zip");
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

        CreateDirectoryW((g_installPath + L"\\client").c_str(), nullptr);
        HANDLE hm = CreateFileW(ClientMarker().c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hm != INVALID_HANDLE_VALUE) CloseHandle(hm);

        g_clientInstalled = true;
        g_freshInstall    = true;
        PostMessageW(g_hwnd, WM_SET_INSTALL_MODE, 1, 0);
        PostPct(100);
    }

    // ── 2. Check HermesProxy update ───────────────────────────────────────────
    PostStatus(WS_CHECKING);
    PostPct(0);

    std::wstring apiUrl = std::wstring(L"https://api.github.com/repos/")
        + HERMES_GH_OWNER + L"/" + HERMES_GH_REPO + L"/releases/latest";

    std::string json = HttpGet(apiUrl);
    if (!json.empty()) {
        std::string tag = JsonString(json, "tag_name");
        std::wstring remoteVer(tag.begin(), tag.end());
        std::wstring localVer = ReadLocalHermesVersion();

        if (!remoteVer.empty() && IsNewer(localVer, remoteVer)) {
            PostStatus(WS_DL_HERMES);
            PostPct(0);

            std::string assetUrl = FindAssetUrl(json);
            if (!assetUrl.empty()) {
                std::wstring assetW(assetUrl.begin(), assetUrl.end());
                std::wstring tmpZip = TempFile(L"hermes_update.zip");

                DlProgress dlHermes{L"Downloading HermesProxy update...", 70};
                bool ok = HttpDownload(assetW, tmpZip,
                    [&dlHermes](DWORD64 dl, DWORD64 tot) { dlHermes(dl, tot); });

                if (ok) {
                    PostStatus(WS_EX_HERMES);
                    std::wstring clientDir = g_installPath + L"\\client";
                    CreateDirectoryW(clientDir.c_str(), nullptr);

                    ok = ExtractZipSmart(tmpZip, clientDir, true, [](int pct) {
                        PostPct(70 + pct * 30 / 100);
                    });
                    DeleteFileW(tmpZip.c_str());

                    if (ok) {
                        WriteLocalHermesVersion(remoteVer);
                        PostPct(100);
                    }
                } else {
                    DeleteFileW(tmpZip.c_str());
                }
            }
        }
    }

    // ── 3. Check WOW_HC addon update ──────────────────────────────────────────
    PostStatus(WS_CHECKING);
    PostPct(0);

    std::wstring addonApiUrl = std::wstring(L"https://api.github.com/repos/")
        + ADDON_GH_OWNER + L"/" + ADDON_GH_REPO + L"/releases/latest";

    std::string addonJson = HttpGet(addonApiUrl);
    if (!addonJson.empty()) {
        std::string addonTag = JsonString(addonJson, "tag_name");
        std::wstring addonRemoteVer(addonTag.begin(), addonTag.end());
        std::wstring addonLocalVer = ReadLocalAddonVersion();
        std::wstring addonFolderPath = g_installPath + L"\\client\\_classic_era_\\Interface\\AddOns\\WOW_HC";
        bool addonFolderMissing = GetFileAttributesW(addonFolderPath.c_str()) == INVALID_FILE_ATTRIBUTES;

        if (!addonRemoteVer.empty() && (IsNewer(addonLocalVer, addonRemoteVer) || addonFolderMissing)) {
            PostStatus(WS_DL_ADDON);
            PostPct(0);

            std::string addonAssetUrl = FindAssetUrl(addonJson);
            if (addonAssetUrl.empty()) {
                // No uploaded binary asset — fall back to GitHub's source ZIP
                addonAssetUrl = JsonString(addonJson, "zipball_url");
            }
            if (!addonAssetUrl.empty()) {
                std::wstring addonAssetW(addonAssetUrl.begin(), addonAssetUrl.end());
                std::wstring tmpZip = TempFile(L"addon_update.zip");

                DlProgress dlAddon{L"Downloading WOW_HC addon...", 70};
                bool ok = HttpDownload(addonAssetW, tmpZip,
                    [&dlAddon](DWORD64 dl, DWORD64 tot) { dlAddon(dl, tot); });

                if (ok) {
                    PostStatus(WS_EX_ADDON);
                    std::wstring addonsDir = g_installPath + L"\\client\\_classic_era_\\Interface\\AddOns";
                    CreateDirectoryW((g_installPath + L"\\client\\_classic_era_").c_str(), nullptr);
                    CreateDirectoryW((g_installPath + L"\\client\\_classic_era_\\Interface").c_str(), nullptr);
                    CreateDirectoryW(addonsDir.c_str(), nullptr);

                    // Remove old addon folder so stale files are not kept
                    std::wstring addonDest = addonsDir + L"\\WOW_HC";
                    DeleteDirRecursive(addonDest);
                    CreateDirectoryW(addonDest.c_str(), nullptr);

                    // Extract into AddOns\WOW_HC; stripTopLevel strips the GitHub archive wrapper
                    ok = ExtractZipSmart(tmpZip, addonDest, true, [](int pct) {
                        PostPct(70 + pct * 30 / 100);
                    });
                    DeleteFileW(tmpZip.c_str());

                    if (ok) {
                        WriteLocalAddonVersion(addonRemoteVer);
                        PostPct(100);
                    }
                } else {
                    DeleteFileW(tmpZip.c_str());
                }
            }
        }
    }

    PostStatus(WS_READY);
    PostPct(100);
    g_playReady = true;
    PostMessageW(g_hwnd, WM_WORKER_DONE, 1, 0);
    g_workerBusy = false;
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

// ── Transfer worker ────────────────────────────────────────────────────────────
// srcClassicEra is the selected _classic_era_ folder from the old installation.
static void TransferWorker(std::wstring srcClassicEra)
{
    g_workerBusy = true;

    std::wstring dstBase    = g_installPath + L"\\client\\_classic_era_";
    std::wstring srcWtf     = srcClassicEra + L"\\WTF";
    std::wstring srcAddons  = srcClassicEra + L"\\Interface\\AddOns";
    std::wstring dstWtf     = dstBase + L"\\WTF";
    std::wstring dstAddons  = dstBase + L"\\Interface\\AddOns";

    // AccountData lives one level above _classic_era_, not inside it
    std::wstring srcParent      = srcClassicEra.substr(0, srcClassicEra.rfind(L'\\'));
    std::wstring srcAccountData = srcParent + L"\\AccountData";
    std::wstring dstAccountData = g_installPath + L"\\client\\AccountData";
    bool hasAccountData = GetFileAttributesW(srcAccountData.c_str()) != INVALID_FILE_ATTRIBUTES;

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
            D(160), D(157), D(200), D(16), hwnd,
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
            D(10), D(268), D(375), D(22), hwnd,
            (HMENU)(UINT_PTR)ID_EDIT_PATH, nullptr, nullptr);
        SF(g_hwndPath, g_fontNormal);

        g_hwndBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            D(393), D(268), D(97), D(22), hwnd,
            (HMENU)(UINT_PTR)ID_BTN_BROWSE, nullptr, nullptr);
        SF(g_hwndBrowse, g_fontNormal);

        // Play/Install — centered, taller primary action button (1.5× base size)
        g_hwndPlay = CreateWindowExW(0, L"BUTTON", L"INSTALL",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
            D(162), D(305), D(195), D(54), hwnd,
            (HMENU)(UINT_PTR)ID_BTN_PLAY, nullptr, nullptr);
        SF(g_hwndPlay, g_fontPlay);

        // Transfer — smaller secondary button below Play
        g_hwndTransfer = CreateWindowExW(0, L"BUTTON",
            L"Transfer UI/Macros/Addons/Settings from existing installation",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
            D(60), D(380), D(400), D(22), hwnd,
            (HMENU)(UINT_PTR)ID_BTN_TRANSFER, nullptr, nullptr);
        SF(g_hwndTransfer, g_fontNormal);

        SetWindowSubclass(g_hwndPlay,     BtnSubclassProc, 0, (DWORD_PTR)&g_playHover);
        SetWindowSubclass(g_hwndBrowse,   BtnSubclassProc, 1, (DWORD_PTR)&g_browseHover);
        SetWindowSubclass(g_hwndTransfer, BtnSubclassProc, 2, (DWORD_PTR)&g_transferHover);

        if (!g_installPath.empty()) {
            g_workerBusy = true;
            std::thread(Worker).detach();
        } else {
            PostStatus(WS_NO_PATH);
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_BROWSE, BN_CLICKED), 0);
        }

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
            int areaX = MulDiv(105,  g_dpi, 96);
            int areaY = MulDiv(40,   g_dpi, 96);
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

        if (id == ID_BTN_BROWSE) {
            IFileOpenDialog* pDlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg)))) {
                DWORD opts = 0; pDlg->GetOptions(&opts);
                pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                pDlg->SetTitle(L"Choose WoW installation folder");
                if (SUCCEEDED(pDlg->Show(hwnd))) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                        wchar_t* path = nullptr;
                        pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
                        if (path) {
                            g_installPath = path;
                            CoTaskMemFree(path);
                            SetWindowTextW(g_hwndPath, g_installPath.c_str());
                            SaveConfig();
                            if (!g_workerBusy.load()) {
                                g_workerBusy = true;
                                std::thread(Worker).detach();
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

            if (!installed && !g_installPath.empty()) {
                g_workerBusy = true;
                g_playReady  = false;
                RefreshPlayButton();
                std::thread(Worker).detach();
            }
            else if (installed && g_playReady.load()) {
                EnableWindow(g_hwndPlay, FALSE);
                PostStatus(WS_LAUNCHING);
                std::wstring path = g_installPath;
                std::thread([path]() {
                    std::wstring clientDir  = path + L"\\client";
                    std::wstring hermesExe  = clientDir + L"\\HermesProxy.exe";
                    std::wstring arctiumExe = clientDir + L"\\Arctium WoW Launcher.exe";

                    if (IsProcessRunning(L"HermesProxy.exe")) {
                        KillProcess(L"HermesProxy.exe");
                        Sleep(500);
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
                    pDlg->SetTitle(L"Select the _classic_era_ folder from your old WoW installation");
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

                // Validate: WowClassic.exe must be directly in the selected folder
                std::wstring wowExe = picked + L"\\WowClassic.exe";
                if (GetFileAttributesW(wowExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    // Check that this is a 1.14.x (Classic Era) build
                    int verMaj = 0, verMin = 0, verPatch = 0;
                    if (GetExeVersion(wowExe, verMaj, verMin, verPatch)
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
                    std::thread([picked]() { TransferWorker(picked); }).detach();
                    break;
                }

                MessageBoxW(hwnd,
                    L"WowClassic.exe was not found in the selected folder.\n\n"
                    L"Please select the \"_classic_era_\" folder that directly contains WowClassic.exe.",
                    L"Wrong folder",
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
        bool ok = (wp != 0) && !g_installPath.empty();
        g_playReady = ok;
        if (ok) g_clientInstalled = true;
        g_taskbarHasProgress = false;
        RefreshPlayButton();
        if (ok) PostStatus(WS_READY);
        if (g_pTaskbar)
            g_pTaskbar->SetProgressState(g_hwnd, ok ? TBPF_NOPROGRESS : TBPF_ERROR);

        if (ok && g_freshInstall) {
            g_freshInstall = false;
            int resp = MessageBoxW(hwnd,
                L"WoW client installed successfully!\r\n\r\n"
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

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis->hwndItem != g_hwndPlay &&
            dis->hwndItem != g_hwndBrowse &&
            dis->hwndItem != g_hwndTransfer)
            break;

        RECT rc       = dis->rcItem;
        HDC  hdc      = dis->hDC;
        bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
        bool disabled = (dis->itemState & ODS_DISABLED)  != 0;
        bool isPlay   = (dis->hwndItem == g_hwndPlay);
        bool hover    = !disabled && (isPlay ? g_playHover :
                        (dis->hwndItem == g_hwndBrowse) ? g_browseHover : g_transferHover);

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
        LONG w = MulDiv(520, g_dpi, 96);
        LONG h = MulDiv(450, g_dpi, 96);
        mmi->ptMinTrackSize = {w, h};
        mmi->ptMaxTrackSize = {w, h};
        break;
    }

    case WM_DESTROY:
        if (g_pTaskbar) { g_pTaskbar->Release(); g_pTaskbar = nullptr; }
        if (g_hIconLarge) { DestroyIcon(g_hIconLarge); g_hIconLarge = nullptr; }
        if (g_hIconSmall) { DestroyIcon(g_hIconSmall); g_hIconSmall = nullptr; }
        if (g_fontLink)   { DeleteObject(g_fontLink);  g_fontLink   = nullptr; }
        PostQuitMessage(0);
        break;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Desktop shortcut ───────────────────────────────────────────────────────────
static void CreateDesktopShortcutIfNeeded()
{
    wchar_t desktop[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop)))
        return;

    std::wstring lnkPath = std::wstring(desktop) + L"\\WOW-HC.lnk";
    if (GetFileAttributesW(lnkPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return;

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    exeDir = exeDir.substr(0, exeDir.rfind(L'\\'));

    IShellLinkW* psl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void**>(&psl))))
        return;

    psl->SetPath(exePath);
    psl->SetWorkingDirectory(exeDir.c_str());
    psl->SetIconLocation(exePath, 0);
    psl->SetDescription(L"WOW HC Launcher");

    IPersistFile* ppf = nullptr;
    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf)))) {
        ppf->Save(lnkPath.c_str(), TRUE);
        ppf->Release();
    }
    psl->Release();
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
    CreateDesktopShortcutIfNeeded();

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

    int WND_W = MulDiv(520, g_dpi, 96);
    int WND_H = MulDiv(450, g_dpi, 96);
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
