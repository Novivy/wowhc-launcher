#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <shlwapi.h>

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

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

// ── Build-time config ──────────────────────────────────────────────────────────
// TODO: replace with the actual client ZIP download URL
static constexpr wchar_t CLIENT_DOWNLOAD_URL[] =
    L"https://dl.wow-hc.com/clients/WOW-Classic-1.14.2.zip";

static constexpr wchar_t APP_NAME[]        = L"WOW HC Launcher";
static constexpr wchar_t HERMES_GH_OWNER[] = L"Novivy";
static constexpr wchar_t HERMES_GH_REPO[]  = L"HermesProxy";

// ── Control / message IDs ──────────────────────────────────────────────────────
enum : UINT {
    ID_BTN_PLAY    = 101,
    ID_BTN_BROWSE  = 102,
    ID_EDIT_PATH   = 103,
    ID_PROGRESS    = 104,
    ID_STATIC_STATUS = 105,
};

#define WM_WORKER_STATUS   (WM_APP + 1)  // wParam = WorkerStatus
#define WM_WORKER_PROGRESS (WM_APP + 2)  // wParam = 0-100
#define WM_WORKER_DONE     (WM_APP + 3)  // wParam = 1 success / 0 fail
#define WM_WORKER_TEXT     (WM_APP + 4)  // lParam = new std::wstring* (UI deletes it)

enum WorkerStatus : int {
    WS_CHECKING = 0,
    WS_DL_HERMES,
    WS_EX_HERMES,
    WS_DL_CLIENT,
    WS_EX_CLIENT,
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
    L"Ready to play!",
    L"Error - check your connection or installation path.",
    L"Launching...",
    L"Select an installation folder, then click Browse.",
};

// ── Globals ────────────────────────────────────────────────────────────────────
static HWND g_hwnd         = nullptr;
static HWND g_hwndStatus   = nullptr;
static HWND g_hwndProgress = nullptr;
static HWND g_hwndPath     = nullptr;
static HWND g_hwndPlay     = nullptr;
static HWND g_hwndBrowse   = nullptr;

static HFONT g_fontNormal  = nullptr;
static HFONT g_fontTitle   = nullptr;
static HFONT g_fontPlay    = nullptr;

static std::wstring g_installPath;
static std::wstring g_configDir;

static std::atomic<bool> g_workerBusy{false};
static std::atomic<bool> g_playReady{false};

static UINT g_dpi = 96; // set in wWinMain before any window is created

// ── Config helpers ─────────────────────────────────────────────────────────────
static std::wstring ConfigPath()  { return g_configDir + L"\\launcher.ini"; }
static std::wstring HermesVerPath() {
    return g_installPath + L"\\client\\hermes_version.txt";
}

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
    // trim whitespace
    while (!v.empty() && (v.back() == L'\r' || v.back() == L'\n' || v.back() == L' '))
        v.pop_back();
    return v;
}

static void WriteLocalHermesVersion(const std::wstring& ver)
{
    std::wofstream f(HermesVerPath());
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

// Returns true if remoteVer is strictly newer than localVer
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
    size_t start = pos + 1;
    size_t end   = start;
    while (end < json.size()) {
        if (json[end] == '"' && json[end-1] != '\\') break;
        ++end;
    }
    if (end >= json.size()) return {};
    return json.substr(start, end - start);
}

// Find a browser_download_url preferring Windows assets, else first zip
static std::string FindHermesAssetUrl(const std::string& json)
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
            if (lower.find("win") != std::string::npos)
                return url; // prefer windows asset
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
    HINTERNET h = WinHttpOpen(L"WOWHCLauncher/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    return h;
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

static HINTERNET MakeRequest(HINTERNET hSess,
    const std::wstring& url, HINTERNET& hConn)
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

// Simple GET — returns response body as string
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

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return body;
}

// Download URL to file, calling progress(downloaded, total) periodically
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
            DWORD64 downloaded = 0;
            DWORD read = 0;
            ok = true;
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

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return ok;
}

// ── ZIP extraction via hidden PowerShell ───────────────────────────────────────
static bool ExtractZip(const std::wstring& zip, const std::wstring& destDir)
{
    // Escape single quotes in paths for PS
    auto escPS = [](const std::wstring& s) {
        std::wstring r;
        for (wchar_t c : s) { if (c == L'\'') r += L"''"; else r += c; }
        return r;
    };

    std::wstring cmd =
        L"powershell.exe -NonInteractive -WindowStyle Hidden -Command "
        L"\"Expand-Archive -Force -LiteralPath '" + escPS(zip) +
        L"' -DestinationPath '" + escPS(destDir) + L"'\"";

    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return exitCode == 0;
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

// ── Temp path ──────────────────────────────────────────────────────────────────
static std::wstring TempFile(const std::wstring& name)
{
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + name;
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

// Stateful progress callback: tracks speed, posts rich status text + progress bar.
// pctMax: the bar goes 0 → pctMax during this download phase.
struct DlProgress {
    std::wstring label;   // e.g. L"Downloading WoW client…"
    int          pctMax;  // ceiling percent for this phase

    DWORD64 lastTick  = 0;
    DWORD64 lastBytes = 0;
    DWORD64 speed     = 0; // bytes/sec rolling

    void operator()(DWORD64 dl, DWORD64 tot)
    {
        DWORD64 now = GetTickCount64();
        if (lastTick == 0) lastTick = now;

        DWORD64 elapsed = now - lastTick;
        if (elapsed >= 1000) {
            DWORD64 delta = (dl > lastBytes) ? dl - lastBytes : 0;
            speed = delta * 1000 / elapsed; // elapsed >= 1000 here, always > 0
            lastBytes = dl;
            lastTick  = now;
        }

        if (tot > 0)
            PostPct((int)(dl * pctMax / tot));

        // Only post text update once per second to avoid blinking
        if (elapsed < 1000 && lastTick != 0 && speed != 0)
            return;

        std::wstring text = label + L"   ";
        if (tot > 0) {
            int pct_val = (int)(dl * 100 / tot);
            wchar_t pctBuf[8];
            swprintf_s(pctBuf, L"%d%%  ", pct_val);
            text += pctBuf;
            text += FmtBytes(dl) + L" / " + FmtBytes(tot);
        } else {
            text += FmtBytes(dl);
        }
        if (speed > 0)
            text += L"   -   " + FmtSpeed(speed);

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
        std::wstring check = g_installPath + L"\\client\\Arctium WoW Launcher.exe";
        clientOk = GetFileAttributesW(check.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    if (!clientOk) {
        PostStatus(WS_DL_CLIENT);
        PostPct(0);

        std::wstring tmpZip = TempFile(L"wowclient_dl.zip");
        DlProgress dlClient{L"Downloading WoW client...", 90};
        bool ok = HttpDownload(CLIENT_DOWNLOAD_URL, tmpZip,
            [&dlClient](DWORD64 dl, DWORD64 tot) { dlClient(dl, tot); });

        if (!ok) {
            PostStatus(WS_ERROR);
            g_workerBusy = false;
            return;
        }

        PostPct(65);
        PostStatus(WS_EX_CLIENT);
        CreateDirectoryW(g_installPath.c_str(), nullptr);

        ok = ExtractZip(tmpZip, g_installPath);
        DeleteFileW(tmpZip.c_str());

        if (!ok) {
            PostStatus(WS_ERROR);
            g_workerBusy = false;
            return;
        }
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

            std::string assetUrl = FindHermesAssetUrl(json);
            if (!assetUrl.empty()) {
                std::wstring assetW(assetUrl.begin(), assetUrl.end());
                std::wstring tmpZip = TempFile(L"hermes_update.zip");

                DlProgress dlHermes{L"Downloading HermesProxy update...", 70};
                bool ok = HttpDownload(assetW, tmpZip,
                    [&dlHermes](DWORD64 dl, DWORD64 tot) { dlHermes(dl, tot); });

                if (ok) {
                    PostPct(70);
                    PostStatus(WS_EX_HERMES);

                    std::wstring clientDir = g_installPath + L"\\client";
                    CreateDirectoryW(clientDir.c_str(), nullptr);

                    ok = ExtractZip(tmpZip, clientDir);
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
    // Don't block play on update failure — just continue

    PostStatus(WS_READY);
    PostPct(100);
    g_playReady = true;
    PostMessageW(g_hwnd, WM_WORKER_DONE, 1, 0);
    g_workerBusy = false;
}

// ── Window procedure ───────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        // Scale a base-96-DPI logical pixel value to the actual system DPI
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
        g_fontTitle  = MakeFont(13, FW_BOLD);
        g_fontPlay   = MakeFont(10, FW_BOLD);

        auto SF = [](HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); };

        // Title
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"WOW HC Private Server",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, D(12), D(500), D(24), hwnd, nullptr, nullptr, nullptr);
        SF(hTitle, g_fontTitle);

        // Status
        g_hwndStatus = CreateWindowExW(0, L"STATIC", STATUS_TEXT[WS_NO_PATH],
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            D(10), D(44), D(480), D(18), hwnd, (HMENU)(UINT_PTR)ID_STATIC_STATUS, nullptr, nullptr);
        SF(g_hwndStatus, g_fontNormal);

        // Progress bar
        g_hwndProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            D(10), D(66), D(480), D(16), hwnd, (HMENU)(UINT_PTR)ID_PROGRESS, nullptr, nullptr);
        SendMessageW(g_hwndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        // Separator
        CreateWindowExW(0, L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            D(10), D(94), D(480), D(2), hwnd, nullptr, nullptr, nullptr);

        // Path label
        HWND hLabel = CreateWindowExW(0, L"STATIC", L"Installation path:",
            WS_CHILD | WS_VISIBLE,
            D(10), D(105), D(480), D(16), hwnd, nullptr, nullptr, nullptr);
        SF(hLabel, g_fontNormal);

        // Path edit
        g_hwndPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            g_installPath.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
            D(10), D(123), D(375), D(22), hwnd, (HMENU)(UINT_PTR)ID_EDIT_PATH, nullptr, nullptr);
        SF(g_hwndPath, g_fontNormal);

        // Browse button
        g_hwndBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            D(393), D(123), D(97), D(22), hwnd, (HMENU)(UINT_PTR)ID_BTN_BROWSE, nullptr, nullptr);
        SF(g_hwndBrowse, g_fontNormal);

        // Play button
        g_hwndPlay = CreateWindowExW(0, L"BUTTON", L"PLAY",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_DISABLED,
            D(185), D(158), D(130), D(34), hwnd, (HMENU)(UINT_PTR)ID_BTN_PLAY, nullptr, nullptr);
        SF(g_hwndPlay, g_fontPlay);

        if (!g_installPath.empty()) {
            std::thread(Worker).detach();
        } else {
            PostStatus(WS_NO_PATH);
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_BROWSE, BN_CLICKED), 0);
        }

        break;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wp);

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
                            if (!g_workerBusy)
                                std::thread(Worker).detach();
                        }
                        pItem->Release();
                    }
                }
                pDlg->Release();
            }
        }
        else if (id == ID_BTN_PLAY && g_playReady.load()) {
            EnableWindow(g_hwndPlay, FALSE);
            PostStatus(WS_LAUNCHING);

            std::wstring path = g_installPath; // capture
            std::thread([path]() {
                std::wstring clientDir = path + L"\\client";
                std::wstring hermesExe = clientDir + L"\\HermesProxy.exe";
                std::wstring arctiumExe = clientDir + L"\\Arctium WoW Launcher.exe";

                if (!IsProcessRunning(L"HermesProxy.exe"))
                    LaunchExe(hermesExe, L"");

                Sleep(1500);
                LaunchExe(arctiumExe, L"--staticseed --version=ClassicEra");

                // Re-enable after a moment
                Sleep(2000);
                PostMessageW(g_hwnd, WM_WORKER_DONE, 1, 0);
            }).detach();
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
        SendMessageW(g_hwndProgress, PBM_SETPOS, wp, 0);
        break;

    case WM_WORKER_DONE:
    {
        bool ok = (wp != 0) && !g_installPath.empty();
        g_playReady = ok;
        EnableWindow(g_hwndPlay, ok ? TRUE : FALSE);
        if (ok) PostStatus(WS_READY);
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_ERASEBKGND:
    {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        LONG w = MulDiv(520, g_dpi, 96);
        LONG h = MulDiv(240, g_dpi, 96);
        mmi->ptMinTrackSize = {w, h};
        mmi->ptMaxTrackSize = {w, h};
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Entry point ────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    // High-DPI: declare awareness before any windows are created.
    // The app.manifest already declares PerMonitorV2; this call covers
    // environments where the manifest isn't embedded (e.g. debugger injection).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_dpi = GetDpiForSystem();

    // Single-instance guard
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"WOWHCLauncherMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"WOWHCLauncherWnd", nullptr);
        if (existing) { SetForegroundWindow(existing); }
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    // Config dir
    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    g_configDir = std::wstring(appdata) + L"\\WOWHCLauncher";
    CreateDirectoryW(g_configDir.c_str(), nullptr);
    LoadConfig();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"WOWHCLauncherWnd";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // Window: fixed size, no resize/maximize — scaled to actual DPI
    int WND_W = MulDiv(520, g_dpi, 96);
    int WND_H = MulDiv(240, g_dpi, 96);
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowExW(0, L"WOWHCLauncherWnd", APP_NAME,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (sx - WND_W) / 2, (sy - WND_H) / 2, WND_W, WND_H,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    CoUninitialize();
    return (int)m.wParam;
}
