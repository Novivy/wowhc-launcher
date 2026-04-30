#include "replay_buffer.h"

#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <gdiplus.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// ── FFmpeg dynamic loading ────────────────────────────────────────────────────
// Load FFmpeg DLLs via LoadLibrary so the EXE starts without them present.
// RB_Start() calls LoadFFmpegDynamic() before engaging any FFmpeg code.
static HMODULE g_ff_avutil   = nullptr;
static HMODULE g_ff_avcodec  = nullptr;
static HMODULE g_ff_avformat = nullptr;
static HMODULE g_ff_swscale  = nullptr;
static std::wstring g_ffmpegDllDir;
static std::wstring g_rbLogPath;

static void RbLog(const wchar_t* fmt, ...)
{
    if (g_rbLogPath.empty()) return;
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t header[32], body[512];
    swprintf_s(header, L"[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vswprintf_s(body, fmt, a); va_end(a);
    std::wstring line = header + std::wstring(body) + L"\r\n";
    int sz = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (sz <= 1) return;
    std::string u8(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, u8.data(), sz, nullptr, nullptr);
    HANDLE hf = CreateFileW(g_rbLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD written; WriteFile(hf, u8.c_str(), (DWORD)u8.size(), &written, nullptr);
        CloseHandle(hf);
    }
}

static AVFrame*           (*ff_av_frame_alloc)              ()                                                                     = nullptr;
static void               (*ff_av_frame_free)               (AVFrame**)                                                            = nullptr;
static int                (*ff_av_frame_make_writable)      (AVFrame*)                                                             = nullptr;
static int                (*ff_av_frame_get_buffer)         (AVFrame*, int)                                                        = nullptr;
static int                (*ff_av_opt_set)                  (void*, const char*, const char*, int)                                 = nullptr;
static int                (*ff_av_dict_set)                 (AVDictionary**, const char*, const char*, int)                        = nullptr;
static void               (*ff_av_dict_free)                (AVDictionary**)                                                       = nullptr;
static const AVCodec*     (*ff_avcodec_find_encoder_by_name)(const char*)                                                          = nullptr;
static AVCodecContext*    (*ff_avcodec_alloc_context3)       (const AVCodec*)                                                      = nullptr;
static int                (*ff_avcodec_open2)               (AVCodecContext*, const AVCodec*, AVDictionary**)                      = nullptr;
static void               (*ff_avcodec_free_context)        (AVCodecContext**)                                                     = nullptr;
static AVCodecParameters* (*ff_avcodec_parameters_alloc)    ()                                                                     = nullptr;
static int                (*ff_avcodec_parameters_from_context)(AVCodecParameters*, const AVCodecContext*)                         = nullptr;
static int                (*ff_avcodec_parameters_copy)     (AVCodecParameters*, const AVCodecParameters*)                         = nullptr;
static void               (*ff_avcodec_parameters_free)     (AVCodecParameters**)                                                  = nullptr;
static int                (*ff_avcodec_send_frame)          (AVCodecContext*, const AVFrame*)                                      = nullptr;
static int                (*ff_avcodec_receive_packet)      (AVCodecContext*, AVPacket*)                                           = nullptr;
static AVPacket*          (*ff_av_packet_alloc)             ()                                                                     = nullptr;
static void               (*ff_av_packet_free)              (AVPacket**)                                                           = nullptr;
static void               (*ff_av_packet_unref)             (AVPacket*)                                                            = nullptr;
static int                (*ff_av_new_packet)               (AVPacket*, int)                                                       = nullptr;
static void               (*ff_av_packet_rescale_ts)        (AVPacket*, AVRational, AVRational)                                    = nullptr;
static int                (*ff_avformat_alloc_output_context2)(AVFormatContext**, const AVOutputFormat*, const char*, const char*) = nullptr;
static AVStream*          (*ff_avformat_new_stream)         (AVFormatContext*, const AVCodec*)                                     = nullptr;
static void               (*ff_avformat_free_context)       (AVFormatContext*)                                                     = nullptr;
static int                (*ff_avformat_write_header)       (AVFormatContext*, AVDictionary**)                                     = nullptr;
static int                (*ff_avio_open)                   (AVIOContext**, const char*, int)                                      = nullptr;
static int                (*ff_avio_closep)                 (AVIOContext**)                                                        = nullptr;
static int                (*ff_av_write_trailer)            (AVFormatContext*)                                                     = nullptr;
static int                (*ff_av_interleaved_write_frame)  (AVFormatContext*, AVPacket*)                                          = nullptr;
static SwsContext*        (*ff_sws_getContext)(int, int, AVPixelFormat, int, int, AVPixelFormat, int, SwsFilter*, SwsFilter*, const double*) = nullptr;
static void               (*ff_sws_freeContext)(SwsContext*)                                                                       = nullptr;
static int                (*ff_sws_scale)(SwsContext*, const uint8_t* const*, const int*, int, int, uint8_t* const*, const int*)   = nullptr;

// Redirect FFmpeg function names to pointers (placed after includes so headers see originals)
#define av_frame_alloc                  ff_av_frame_alloc
#define av_frame_free                   ff_av_frame_free
#define av_frame_make_writable          ff_av_frame_make_writable
#define av_frame_get_buffer             ff_av_frame_get_buffer
#define av_opt_set                      ff_av_opt_set
#define av_dict_set                     ff_av_dict_set
#define av_dict_free                    ff_av_dict_free
#define avcodec_find_encoder_by_name    ff_avcodec_find_encoder_by_name
#define avcodec_alloc_context3          ff_avcodec_alloc_context3
#define avcodec_open2                   ff_avcodec_open2
#define avcodec_free_context            ff_avcodec_free_context
#define avcodec_parameters_alloc        ff_avcodec_parameters_alloc
#define avcodec_parameters_from_context ff_avcodec_parameters_from_context
#define avcodec_parameters_copy         ff_avcodec_parameters_copy
#define avcodec_parameters_free         ff_avcodec_parameters_free
#define avcodec_send_frame              ff_avcodec_send_frame
#define avcodec_receive_packet          ff_avcodec_receive_packet
#define av_packet_alloc                 ff_av_packet_alloc
#define av_packet_free                  ff_av_packet_free
#define av_packet_unref                 ff_av_packet_unref
#define av_new_packet                   ff_av_new_packet
#define av_packet_rescale_ts            ff_av_packet_rescale_ts
#define avformat_alloc_output_context2  ff_avformat_alloc_output_context2
#define avformat_new_stream             ff_avformat_new_stream
#define avformat_free_context           ff_avformat_free_context
#define avformat_write_header           ff_avformat_write_header
#define avio_open                       ff_avio_open
#define avio_closep                     ff_avio_closep
#define av_write_trailer                ff_av_write_trailer
#define av_interleaved_write_frame      ff_av_interleaved_write_frame
#define sws_getContext                  ff_sws_getContext
#define sws_freeContext                 ff_sws_freeContext
#define sws_scale                       ff_sws_scale

static bool LoadFFmpegDynamic()
{
    if (g_ff_avcodec) return true;

    std::wstring dllDir;
    if (!g_ffmpegDllDir.empty()) {
        dllDir = g_ffmpegDllDir;
        if (dllDir.back() != L'\\') dllDir += L'\\';
    } else {
        wchar_t exeDirBuf[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exeDirBuf, MAX_PATH);
        if (wchar_t* sl = wcsrchr(exeDirBuf, L'\\')) sl[1] = L'\0';
        dllDir = exeDirBuf;
    }
    RbLog(L"LoadFFmpeg: searching dir='%s'", dllDir.c_str());

    auto Load = [&](const wchar_t* dll) -> HMODULE {
        HMODULE h = LoadLibraryW((dllDir + dll).c_str());
        if (!h) RbLog(L"LoadFFmpeg: FAILED to load %s (error %lu)", dll, GetLastError());
        else    RbLog(L"LoadFFmpeg: loaded %s", dll);
        return h;
    };

    // Load in dependency order: avutil first, then consumers
    HMODULE avutil   = Load(L"avutil-60.dll");
    HMODULE swres    = Load(L"swresample-6.dll");
    HMODULE swscale  = Load(L"swscale-9.dll");
    HMODULE avcodec  = Load(L"avcodec-62.dll");
    HMODULE avformat = Load(L"avformat-62.dll");

    if (!avutil || !swres || !swscale || !avcodec || !avformat) {
        if (avformat) FreeLibrary(avformat);
        if (avcodec)  FreeLibrary(avcodec);
        if (swscale)  FreeLibrary(swscale);
        if (swres)    FreeLibrary(swres);
        if (avutil)   FreeLibrary(avutil);
        return false;
    }

    g_ff_avutil   = avutil;
    g_ff_avcodec  = avcodec;
    g_ff_avformat = avformat;
    g_ff_swscale  = swscale;

#define GP(mod, name) ff_##name = (decltype(ff_##name))GetProcAddress(mod, #name)
    GP(avutil,   av_frame_alloc);             GP(avutil,   av_frame_free);
    GP(avutil,   av_frame_make_writable);     GP(avutil,   av_frame_get_buffer);
    GP(avutil,   av_opt_set);                 GP(avutil,   av_dict_set);
    GP(avutil,   av_dict_free);
    GP(avcodec,  avcodec_find_encoder_by_name); GP(avcodec, avcodec_alloc_context3);
    GP(avcodec,  avcodec_open2);              GP(avcodec,  avcodec_free_context);
    GP(avcodec,  avcodec_parameters_alloc);   GP(avcodec,  avcodec_parameters_from_context);
    GP(avcodec,  avcodec_parameters_copy);    GP(avcodec,  avcodec_parameters_free);
    GP(avcodec,  avcodec_send_frame);         GP(avcodec,  avcodec_receive_packet);
    GP(avcodec,  av_packet_alloc);            GP(avcodec,  av_packet_free);
    GP(avcodec,  av_packet_unref);            GP(avcodec,  av_new_packet);
    GP(avcodec,  av_packet_rescale_ts);
    GP(avformat, avformat_alloc_output_context2); GP(avformat, avformat_new_stream);
    GP(avformat, avformat_free_context);      GP(avformat, avformat_write_header);
    GP(avformat, avio_open);                  GP(avformat, avio_closep);
    GP(avformat, av_write_trailer);           GP(avformat, av_interleaved_write_frame);
    GP(swscale,  sws_getContext);             GP(swscale,  sws_freeContext);
    GP(swscale,  sws_scale);
#undef GP

    if (!ff_av_frame_alloc || !ff_av_frame_free || !ff_av_frame_make_writable ||
        !ff_av_frame_get_buffer || !ff_av_opt_set || !ff_av_dict_set || !ff_av_dict_free ||
        !ff_avcodec_find_encoder_by_name || !ff_avcodec_alloc_context3 || !ff_avcodec_open2 ||
        !ff_avcodec_free_context || !ff_avcodec_parameters_alloc ||
        !ff_avcodec_parameters_from_context || !ff_avcodec_parameters_copy ||
        !ff_avcodec_parameters_free || !ff_avcodec_send_frame || !ff_avcodec_receive_packet ||
        !ff_av_packet_alloc || !ff_av_packet_free || !ff_av_packet_unref ||
        !ff_av_new_packet || !ff_av_packet_rescale_ts ||
        !ff_avformat_alloc_output_context2 || !ff_avformat_new_stream ||
        !ff_avformat_free_context || !ff_avformat_write_header ||
        !ff_avio_open || !ff_avio_closep || !ff_av_write_trailer || !ff_av_interleaved_write_frame ||
        !ff_sws_getContext || !ff_sws_freeContext || !ff_sws_scale)
    {
        FreeLibrary(g_ff_avformat); g_ff_avformat = nullptr;
        FreeLibrary(g_ff_avcodec);  g_ff_avcodec  = nullptr;
        FreeLibrary(g_ff_swscale);  g_ff_swscale  = nullptr;
        FreeLibrary(g_ff_avutil);   g_ff_avutil   = nullptr;
        FreeLibrary(swres);
        return false;
    }

    return true;
}

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <algorithm>
#include <ctime>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static bool   g_showNotifications = false;
void RB_SetShowNotifications(bool show) { g_showNotifications = show; }

// ── OSD window ────────────────────────────────────────────────────────────────
static HWND   g_osdHwnd      = nullptr;
static HFONT  g_osdFont      = nullptr;
static HFONT  g_osdTitleFont = nullptr;
static Gdiplus::Bitmap* g_osdBitmap = nullptr;
static OsdAccent g_osdAccent = OSD_GREEN;
static std::wstring g_osdText;
static constexpr UINT OSD_TIMER = 50;
static constexpr UINT OSD_SHOW_MS = 3000;

#define WM_OSD_SHOWTEXT (WM_APP + 50)  // lParam = new wstring*, wParam = isError

// Forward declarations needed by OsdWndProc
static ReplaySettings g_rbSettings;
static std::vector<MonitorDesc> g_monitorCache;

static Gdiplus::Bitmap* LoadOsdPng(int resId)
{
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), L"PNG");
    if (!hRes) return nullptr;
    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return nullptr;
    DWORD size = SizeofResource(nullptr, hRes);
    HGLOBAL hCopy = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hCopy) return nullptr;
    memcpy(GlobalLock(hCopy), LockResource(hData), size);
    GlobalUnlock(hCopy);
    IStream* pStream = nullptr;
    CreateStreamOnHGlobal(hCopy, TRUE, &pStream);
    auto* bmp = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return nullptr; }
    return bmp;
}

static LRESULT CALLBACK OsdWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_OSD_SHOWTEXT: {
        auto* s = reinterpret_cast<std::wstring*>(lp);
        g_osdText   = *s;
        g_osdAccent = (OsdAccent)(int)wp;
        delete s;
        // Position on the configured recording monitor
        RECT rcMon = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        {
            int monIdx = g_rbSettings.monitorIndex;
            if (!g_monitorCache.empty()) {
                if (monIdx < 0 || monIdx >= (int)g_monitorCache.size()) monIdx = 0;
                MONITORINFO mi = { sizeof(mi) };
                if (GetMonitorInfoW(g_monitorCache[monIdx].hmon, &mi))
                    rcMon = mi.rcMonitor;
            }
        }
        RECT wrc; GetWindowRect(hwnd, &wrc);
        int ow = wrc.right - wrc.left;
        SetWindowPos(hwnd, HWND_TOPMOST, rcMon.right - ow - 20, rcMon.top + 20, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, TRUE);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetTimer(hwnd, OSD_TIMER, OSD_SHOW_MS, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (wp == OSD_TIMER) {
            KillTimer(hwnd, OSD_TIMER);
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        COLORREF bgClr     = RGB(15, 15, 18);
        COLORREF accentClr = g_osdAccent == OSD_RED    ? RGB(200, 60,  60)  :
                             g_osdAccent == OSD_ORANGE ? RGB(210, 130, 20) :
                                                          RGB(60,  180, 90);

        HBRUSH hbrBg = CreateSolidBrush(bgClr);
        FillRect(hdc, &rc, hbrBg);
        DeleteObject(hbrBg);

        RECT accent = { rc.left, rc.top, rc.left + 8, rc.bottom };
        HBRUSH hbrAccent = CreateSolidBrush(accentClr);
        FillRect(hdc, &accent, hbrAccent);
        DeleteObject(hbrAccent);

        // Vertically center the title+message block with equal top/bottom padding
        int titleH = 30;
        int textH  = 22;
        int gap    = 4;
        int blockH = titleH + gap + textH;
        int topY   = rc.top + (rc.bottom - rc.top - blockH) / 2;

        constexpr int ICON_X  = 22;
        constexpr int ICON_SZ = 28;
        if (g_osdBitmap) {
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            int iy = topY + (titleH - ICON_SZ) / 2;
            Gdiplus::RectF dst((Gdiplus::REAL)(rc.left + ICON_X), (Gdiplus::REAL)iy,
                               (Gdiplus::REAL)ICON_SZ, (Gdiplus::REAL)ICON_SZ);
            g.DrawImage(g_osdBitmap, dst);
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 220, 225));
        if (g_osdTitleFont) SelectObject(hdc, g_osdTitleFont);
        RECT titleRc = { rc.left + ICON_X + ICON_SZ + 6, topY - 4, rc.right - 16, topY + titleH - 4 };
        DrawTextW(hdc, L"WOW-HC", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (g_osdFont) SelectObject(hdc, g_osdFont);
        RECT textRc = { rc.left + ICON_X, topY + titleH + gap, rc.right - 16, topY + titleH + gap + textH };
        DrawTextW(hdc, g_osdText.c_str(), -1, &textRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void CreateOsdWindow(HWND hMainWnd)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OsdWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"WOWHCReplayOSD";
    RegisterClassExW(&wc);

    {
        LOGFONTW lf = {};
        lf.lfHeight  = -MulDiv(10, 96, 72);
        lf.lfWeight  = FW_SEMIBOLD;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        g_osdFont = CreateFontIndirectW(&lf);
    }
    {
        LOGFONTW lf = {};
        lf.lfHeight  = -MulDiv(20, 96, 72);
        lf.lfWeight  = FW_BOLD;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        g_osdTitleFont = CreateFontIndirectW(&lf);
    }

    g_osdHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"WOWHCReplayOSD", nullptr,
        WS_POPUP,
        0, 0, 225, 80,
        hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    SetLayeredWindowAttributes(g_osdHwnd, 0, 235, LWA_ALPHA);

    g_osdBitmap = LoadOsdPng(202);
}

// ── Stored packet ─────────────────────────────────────────────────────────────
struct StoredPacket {
    std::vector<uint8_t> data;
    int64_t pts;
    int64_t dts;
    int64_t duration;
    bool    is_keyframe;
    double  wall_ms;   // ms since capture started
};

// ── Module globals ────────────────────────────────────────────────────────────
static HWND          g_rbHwnd    = nullptr;
static std::atomic<bool>    g_rbRunning{false};
static std::atomic<bool>    g_rbSaveRequested{false};
static std::atomic<ULONGLONG> g_rbStartTick{0};
static std::thread   g_rbThread;

static std::deque<StoredPacket> g_packets;
static std::mutex    g_packetMutex;

// ── Helpers ───────────────────────────────────────────────────────────────────
static std::wstring DefaultVideosFolder()
{
    wchar_t buf[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_MYVIDEO, nullptr, 0, buf);
    return buf;
}

static void CreateDirectoryRecursive(const std::wstring& path)
{
    if (path.empty()) return;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        CreateDirectoryRecursive(path.substr(0, pos));
    CreateDirectoryW(path.c_str(), nullptr);
}

static size_t MaxBufferBytes()
{
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    int fps = std::max(20, std::min(60, g_rbSettings.fps));
    int pct = 6 + (fps - 20) * 9 / 40;  // 6% at 20fps, 15% at 60fps
    return (size_t)(ms.ullTotalPhys * pct / 100);
}

static void PushPacket(StoredPacket pkt)
{
    std::lock_guard<std::mutex> lock(g_packetMutex);
    g_packets.push_back(std::move(pkt));

    if (g_packets.empty()) return;
    double newestMs  = g_packets.back().wall_ms;
    double windowMs  = g_rbSettings.minutes * 60.0 * 1000.0;

    while (g_packets.size() > 1 &&
           (newestMs - g_packets.front().wall_ms) > windowMs)
        g_packets.pop_front();

    size_t totalBytes = 0;
    for (auto& p : g_packets) totalBytes += p.data.size();
    size_t maxBytes = MaxBufferBytes();
    while (totalBytes > maxBytes && g_packets.size() > 1) {
        totalBytes -= g_packets.front().data.size();
        g_packets.pop_front();
    }
}

// ── Save buffer to file ───────────────────────────────────────────────────────
static void DoSave(std::deque<StoredPacket> pkts, AVRational timeBase,
                   AVCodecParameters* codecpar, const std::wstring& folder)
{
    if (pkts.empty()) { RB_ShowOsd(L"Nothing to save yet.", OSD_RED); return; }

    double windowMs = g_rbSettings.minutes * 60.0 * 1000.0;
    double newestMs = pkts.back().wall_ms;
    double cutoffMs = newestMs - windowMs;

    size_t startIdx = 0;
    for (size_t i = 0; i < pkts.size(); i++) {
        if (pkts[i].wall_ms >= cutoffMs && pkts[i].is_keyframe) {
            startIdx = i; break;
        }
    }
    while (startIdx < pkts.size() && !pkts[startIdx].is_keyframe) startIdx++;
    if (startIdx >= pkts.size()) { RB_ShowOsd(L"No keyframe found to save.", OSD_RED); return; }

    std::wstring saveDir = folder.empty() ? DefaultVideosFolder() : folder;
    CreateDirectoryRecursive(saveDir);

    if (GetFileAttributesW(saveDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        RB_ShowOsd(L"Cannot create save folder: " + saveDir, OSD_RED);
        return;
    }

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t fname[64];
    swprintf_s(fname, L"WOW-HC-Replay_%04d-%02d-%02d_%02d-%02d-%02d.mp4",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring savePath = saveDir + L"\\" + fname;

    int nameLen = WideCharToMultiByte(CP_UTF8, 0, savePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string savePathA(nameLen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, savePath.c_str(), -1, savePathA.data(), nameLen, nullptr, nullptr);
    savePathA.resize(strlen(savePathA.c_str()));

    AVFormatContext* fmtCtx = nullptr;
    if (avformat_alloc_output_context2(&fmtCtx, nullptr, "mp4", savePathA.c_str()) < 0) {
        RB_ShowOsd(L"Failed to create output context: " + saveDir, OSD_RED);
        return;
    }

    AVStream* st2 = avformat_new_stream(fmtCtx, nullptr);
    avcodec_parameters_copy(st2->codecpar, codecpar);
    st2->codecpar->codec_tag = 0;
    st2->time_base = timeBase;

    if (avio_open(&fmtCtx->pb, savePathA.c_str(), AVIO_FLAG_WRITE) < 0) {
        avformat_free_context(fmtCtx);
        RB_ShowOsd(L"Cannot write to: " + saveDir, OSD_RED);
        return;
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "faststart", 0);
    avformat_write_header(fmtCtx, &opts);
    av_dict_free(&opts);

    int64_t ptsOff = pkts[startIdx].pts;

    for (size_t i = startIdx; i < pkts.size(); i++) {
        auto& sp = pkts[i];
        AVPacket* pkt = av_packet_alloc();
        av_new_packet(pkt, (int)sp.data.size());
        memcpy(pkt->data, sp.data.data(), sp.data.size());
        pkt->pts          = sp.pts - ptsOff;
        pkt->dts          = sp.dts - ptsOff;
        pkt->duration     = sp.duration;
        pkt->stream_index = 0;
        if (sp.is_keyframe) pkt->flags |= AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(pkt, timeBase, st2->time_base);
        av_interleaved_write_frame(fmtCtx, pkt);
        av_packet_free(&pkt);
    }

    av_write_trailer(fmtCtx);
    avio_closep(&fmtCtx->pb);
    avformat_free_context(fmtCtx);
    avcodec_parameters_free(&codecpar);

    if (g_showNotifications)
        RB_ShowOsd(L"Last " + std::to_wstring(g_rbSettings.minutes) + L" minutes of gameplay saved to disk");
}

// ── Encoder auto-detect ───────────────────────────────────────────────────────
static AVCodecContext* OpenBestEncoder(int width, int height, int fps)
{
    static const char* kNames[] = {
        "h264_nvenc", "h264_amf", "h264_qsv", "libx264", nullptr
    };

    for (int i = 0; kNames[i]; i++) {
        const AVCodec* codec = avcodec_find_encoder_by_name(kNames[i]);
        if (!codec) continue;

        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        if (!ctx) continue;

        ctx->width       = width;
        ctx->height      = height;
        ctx->time_base   = { 1, fps };
        ctx->framerate   = { fps, 1 };
        ctx->pix_fmt     = AV_PIX_FMT_YUV420P;
        ctx->gop_size    = fps * 2;
        ctx->max_b_frames = 0;

        int64_t bps = (int64_t)8000000 * width * height / (1920 * 1080);
        bps = std::max(bps, (int64_t)4000000);
        bps = std::min(bps, (int64_t)30000000);
        ctx->bit_rate = bps;

        if (i == 0) { // nvenc
            av_opt_set(ctx->priv_data, "preset",  "p4",  0);
            av_opt_set(ctx->priv_data, "tune",    "ll",  0);
            av_opt_set(ctx->priv_data, "rc",      "cbr", 0);
        } else if (i == 1) { // amf
            av_opt_set(ctx->priv_data, "usage", "lowlatency", 0);
            av_opt_set(ctx->priv_data, "rc",    "cbr",        0);
        } else if (i == 2) { // qsv
            av_opt_set(ctx->priv_data, "preset", "faster", 0);
        } else { // libx264
            av_opt_set(ctx->priv_data, "preset", "veryfast",    0);
            av_opt_set(ctx->priv_data, "tune",   "zerolatency", 0);
        }

        if (avcodec_open2(ctx, codec, nullptr) == 0)
            return ctx;

        avcodec_free_context(&ctx);
    }
    return nullptr;
}

// ── Capture + encode thread ───────────────────────────────────────────────────
static void CaptureThread(int adapterIdx, int outputIdx)
{
    // D3D11 device
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        RB_ShowOsd(L"Failed to create DXGI factory.", OSD_RED);
        g_rbRunning = false;
        return;
    }

    IDXGIAdapter* adapter = nullptr;
    if (factory->EnumAdapters((UINT)adapterIdx, &adapter) == DXGI_ERROR_NOT_FOUND) {
        factory->EnumAdapters(0, &adapter);
    }
    factory->Release();

    ID3D11Device*        d3dDevice  = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDevice(
        adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        0, featureLevels, 3,
        D3D11_SDK_VERSION, &d3dDevice, nullptr, &d3dContext);
    adapter->Release();

    if (FAILED(hr)) {
        RB_ShowOsd(L"Failed to create D3D11 device.", OSD_RED);
        g_rbRunning = false;
        return;
    }

    // Get output for duplication
    IDXGIDevice*  dxgiDevice  = nullptr;
    IDXGIAdapter* dxgiAdapter = nullptr;
    d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);
    dxgiDevice->Release();

    IDXGIOutput*  dxgiOutput  = nullptr;
    IDXGIOutput1* dxgiOutput1 = nullptr;
    if (dxgiAdapter->EnumOutputs((UINT)outputIdx, &dxgiOutput) == DXGI_ERROR_NOT_FOUND)
        dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    dxgiAdapter->Release();

    DXGI_OUTPUT_DESC outDesc = {};
    dxgiOutput->GetDesc(&outDesc);
    dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
    dxgiOutput->Release();

    int frameW = (outDesc.DesktopCoordinates.right  - outDesc.DesktopCoordinates.left) & ~1;
    int frameH = (outDesc.DesktopCoordinates.bottom - outDesc.DesktopCoordinates.top)  & ~1;

    // Clamp encoder output: 720p min, 1080p max; preserve aspect ratio
    int encH = std::max(720, std::min(1080, frameH)) & ~1;
    int encW = ((int)((int64_t)frameW * encH / frameH)) & ~1;

    IDXGIOutputDuplication* dupl = nullptr;
    hr = dxgiOutput1->DuplicateOutput(d3dDevice, &dupl);
    dxgiOutput1->Release();
    if (FAILED(hr)) {
        d3dDevice->Release(); d3dContext->Release();
        RB_ShowOsd(L"Failed to start screen capture.", OSD_RED);
        g_rbRunning = false;
        return;
    }

    // Staging texture for CPU readback
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width          = (UINT)frameW;
    stagingDesc.Height         = (UINT)frameH;
    stagingDesc.MipLevels      = 1;
    stagingDesc.ArraySize      = 1;
    stagingDesc.Format         = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc     = { 1, 0 };
    stagingDesc.Usage          = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* stagingTex = nullptr;
    d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &stagingTex);

    int     fps             = std::max(20, std::min(60, g_rbSettings.fps));
    DWORD64 frameIntervalMs = 1000u / (DWORD64)fps;

    // Encoder
    AVCodecContext* encCtx = OpenBestEncoder(encW, encH, fps);
    if (!encCtx) {
        stagingTex->Release(); dupl->Release();
        d3dDevice->Release(); d3dContext->Release();
        RB_ShowOsd(L"No H.264 encoder found (install GPU drivers or Visual C++ Redist).", OSD_RED);
        g_rbRunning = false;
        return;
    }

    // sws context: BGRA -> YUV420P, scaling capture res down/up to enc res
    SwsContext* sws = sws_getContext(
        frameW, frameH, AV_PIX_FMT_BGRA,
        encW,   encH,   AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    AVFrame* frame = av_frame_alloc();
    frame->format  = AV_PIX_FMT_YUV420P;
    frame->width   = encW;
    frame->height  = encH;
    av_frame_get_buffer(frame, 32);

    AVCodecParameters* savedParams = avcodec_parameters_alloc();
    avcodec_parameters_from_context(savedParams, encCtx);

    LARGE_INTEGER freq, startCounter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&startCounter);

    DWORD64 lastFrameTick = 0;
    int64_t framePts      = 0;

    if (g_showNotifications) RB_ShowOsd(L"Recording Started", OSD_RED);
    PostMessageW(g_rbHwnd, WM_RB_STATUS, 1, 0);

    while (g_rbRunning.load()) {

        if (g_rbSaveRequested.exchange(false)) {
            std::deque<StoredPacket> snap;
            AVCodecParameters* paramsCopy = avcodec_parameters_alloc();
            {
                std::lock_guard<std::mutex> lock(g_packetMutex);
                snap = g_packets;
                avcodec_parameters_copy(paramsCopy, savedParams);
            }
            std::wstring folder = g_rbSettings.saveFolder;
            std::thread([snap = std::move(snap), paramsCopy, timeBase = encCtx->time_base, folder]() mutable {
                DoSave(std::move(snap), timeBase, paramsCopy, folder);
            }).detach();
        }

        IDXGIResource* desktopResource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        hr = dupl->AcquireNextFrame((DWORD)(frameIntervalMs + 5), &frameInfo, &desktopResource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            // Desktop change (resolution/DPI/sleep) — recreate duplication
            dupl->Release(); dupl = nullptr;
            dxgiOutput = nullptr;
            IDXGIDevice*  dev2  = nullptr;
            IDXGIAdapter* adp2  = nullptr;
            d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dev2);
            dev2->GetParent(__uuidof(IDXGIAdapter), (void**)&adp2);
            dev2->Release();
            IDXGIOutput* out2 = nullptr;
            IDXGIOutput1* out1 = nullptr;
            adp2->EnumOutputs((UINT)outputIdx, &out2);
            adp2->Release();
            if (out2) {
                out2->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1);
                out2->Release();
                if (out1) {
                    out1->DuplicateOutput(d3dDevice, &dupl);
                    out1->Release();
                }
            }
            if (!dupl) break;
            Sleep(200);
            continue;
        }

        if (FAILED(hr)) break;

        DWORD64 now = GetTickCount64();
        if (now - lastFrameTick < frameIntervalMs) {
            dupl->ReleaseFrame();
            desktopResource->Release();
            continue;
        }
        lastFrameTick = now;

        ID3D11Texture2D* frameTex = nullptr;
        desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&frameTex);
        desktopResource->Release();

        if (frameTex) {
            d3dContext->CopyResource(stagingTex, frameTex);
            frameTex->Release();
        }
        dupl->ReleaseFrame();

        if (!frameTex) continue;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(d3dContext->Map(stagingTex, 0, D3D11_MAP_READ, 0, &mapped))) {
            av_frame_make_writable(frame);
            const uint8_t* srcData[1]   = { (uint8_t*)mapped.pData };
            int            srcStride[1] = { (int)mapped.RowPitch };
            sws_scale(sws, srcData, srcStride, 0, frameH, frame->data, frame->linesize);
            d3dContext->Unmap(stagingTex, 0);

            LARGE_INTEGER cur;
            QueryPerformanceCounter(&cur);
            double elapsedMs = (double)(cur.QuadPart - startCounter.QuadPart) * 1000.0
                               / (double)freq.QuadPart;
            // Use actual wall-clock time for pts so playback speed is correct
            // regardless of timer jitter or high-refresh-rate monitors.
            int64_t wallPts  = (int64_t)(elapsedMs * fps / 1000.0);
            frame->pts = std::max(framePts, wallPts);
            framePts   = frame->pts + 1;
            avcodec_send_frame(encCtx, frame);

            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(encCtx, pkt) == 0) {
                StoredPacket sp;
                sp.data.assign(pkt->data, pkt->data + pkt->size);
                sp.pts         = pkt->pts;
                sp.dts         = pkt->dts;
                sp.duration    = pkt->duration;
                sp.is_keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
                sp.wall_ms     = elapsedMs;
                PushPacket(std::move(sp));
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }
    }

    // Flush encoder
    avcodec_send_frame(encCtx, nullptr);
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(encCtx, pkt) == 0) av_packet_unref(pkt);
    av_packet_free(&pkt);

    avcodec_parameters_free(&savedParams);
    av_frame_free(&frame);
    sws_freeContext(sws);
    avcodec_free_context(&encCtx);
    stagingTex->Release();
    if (dupl) dupl->Release();
    d3dContext->Release();
    d3dDevice->Release();

    PostMessageW(g_rbHwnd, WM_RB_STATUS, 0, 0);
    if (g_showNotifications) RB_ShowOsd(L"Recording Stopped", OSD_ORANGE);
    g_rbRunning = false;
}

// ── Public API ────────────────────────────────────────────────────────────────
void RB_Init(HWND hMainWnd)
{
    g_rbHwnd = hMainWnd;
    CreateOsdWindow(hMainWnd);
}

void RB_Shutdown()
{
    RB_Stop();
    RB_UnregisterHotkeys();
    if (g_osdHwnd) { DestroyWindow(g_osdHwnd); g_osdHwnd = nullptr; }
    if (g_osdFont) { DeleteObject(g_osdFont); g_osdFont = nullptr; }
}

std::vector<MonitorDesc> RB_EnumMonitors()
{
    std::vector<MonitorDesc> result;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return result;

    UINT adIdx = 0;
    IDXGIAdapter* adapter = nullptr;
    while (factory->EnumAdapters(adIdx, &adapter) != DXGI_ERROR_NOT_FOUND) {
        UINT outIdx = 0;
        IDXGIOutput* output = nullptr;
        while (adapter->EnumOutputs(outIdx, &output) != DXGI_ERROR_NOT_FOUND) {
            DXGI_OUTPUT_DESC desc = {};
            output->GetDesc(&desc);

            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfoW(desc.Monitor, &mi);
            int w = mi.rcMonitor.right  - mi.rcMonitor.left;
            int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
            bool primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

            MonitorDesc md;
            md.hmon       = desc.Monitor;
            md.adapterIdx = (int)adIdx;
            md.outputIdx  = (int)outIdx;

            wchar_t buf[128];
            swprintf_s(buf, L"Monitor %u  %dx%d%s",
                       (unsigned)(result.size() + 1), w, h,
                       primary ? L"  [Primary]" : L"");
            md.name = buf;

            result.push_back(std::move(md));
            output->Release(); outIdx++;
        }
        adapter->Release(); adIdx++;
    }
    factory->Release();
    g_monitorCache = result;
    return result;
}

bool RB_Start()
{
    if (g_rbRunning.load()) return true;

    if (!LoadFFmpegDynamic()) {
        RB_ShowOsd(L"Recording DLLs not ready yet — download in progress.", OSD_ORANGE);
        return false;
    }

    auto monitors = RB_EnumMonitors();
    int idx = g_rbSettings.monitorIndex;
    if (idx < 0 || idx >= (int)monitors.size()) idx = 0;
    if (monitors.empty()) {
        RB_ShowOsd(L"No monitors found.", OSD_RED);
        return false;
    }

    int adapterIdx = monitors[idx].adapterIdx;
    int outputIdx  = monitors[idx].outputIdx;

    // Position OSD on recording monitor
    if (g_osdHwnd) {
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(monitors[idx].hmon, &mi);
        int w = 450, h = 80;
        int x = mi.rcMonitor.right  - w - 20;
        int y = mi.rcMonitor.top + 20;
        SetWindowPos(g_osdHwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    }

    {
        std::lock_guard<std::mutex> lock(g_packetMutex);
        g_packets.clear();
    }

    g_rbRunning  = true;
    g_rbStartTick = GetTickCount64();
    g_rbThread   = std::thread(CaptureThread, adapterIdx, outputIdx);
    return true;
}

void RB_Stop()
{
    if (!g_rbRunning.load()) return;
    g_rbRunning = false;
    if (g_rbThread.joinable()) g_rbThread.join();
}

RbSaveResult RB_SaveNow()
{
    if (!g_rbRunning.load()) {
        RB_ShowOsd(L"Replay buffer is not running.", OSD_RED);
        return RB_SAVE_NOT_RUNNING;
    }
    if (GetTickCount64() - g_rbStartTick.load() < 15000) {
        RB_ShowOsd(L"Recording just started, try again in a few seconds.", OSD_ORANGE);
        return RB_SAVE_TOO_EARLY;
    }
    RB_ShowOsd(L"Saving replay...");
    g_rbSaveRequested = true;
    return RB_SAVE_OK;
}

bool RB_IsRunning()
{
    return g_rbRunning.load();
}

void RB_SetSettings(const ReplaySettings& s)
{
    g_rbSettings = s;
}

void RB_ApplySettings(const ReplaySettings& s)
{
    bool wasRunning = RB_IsRunning();
    if (wasRunning) RB_Stop();
    g_rbSettings = s;
    if (wasRunning) RB_Start();
}

const ReplaySettings& RB_GetSettings()
{
    return g_rbSettings;
}

void RB_ShowOsd(const std::wstring& text, OsdAccent accent)
{
    if (!g_osdHwnd) return;
    PostMessageW(g_osdHwnd, WM_OSD_SHOWTEXT,
                 (WPARAM)(int)accent,
                 (LPARAM)(new std::wstring(text)));
}

int RB_RegisterHotkeys()
{
    int failed = 0;
    if (!g_rbHwnd) return failed;
    UnregisterHotKey(g_rbHwnd, HOTKEY_ID_RB_STARTSTOP);
    UnregisterHotKey(g_rbHwnd, HOTKEY_ID_RB_SAVE);
    if (g_rbSettings.startStopVK) {
        if (!RegisterHotKey(g_rbHwnd, HOTKEY_ID_RB_STARTSTOP,
                            g_rbSettings.startStopMods | MOD_NOREPEAT,
                            g_rbSettings.startStopVK)) {
            g_rbSettings.startStopVK   = 0;
            g_rbSettings.startStopMods = 0;
            failed |= RB_HK_STARTSTOP_FAILED;
        }
    }
    if (g_rbSettings.saveVK) {
        if (!RegisterHotKey(g_rbHwnd, HOTKEY_ID_RB_SAVE,
                            g_rbSettings.saveMods | MOD_NOREPEAT,
                            g_rbSettings.saveVK)) {
            g_rbSettings.saveVK   = 0;
            g_rbSettings.saveMods = 0;
            failed |= RB_HK_SAVE_FAILED;
        }
    }
    return failed;
}

void RB_UnregisterHotkeys()
{
    if (!g_rbHwnd) return;
    UnregisterHotKey(g_rbHwnd, HOTKEY_ID_RB_STARTSTOP);
    UnregisterHotKey(g_rbHwnd, HOTKEY_ID_RB_SAVE);
}

void RB_SetDllDir(const std::wstring& dir) { g_ffmpegDllDir = dir; }
void RB_SetLogPath(const std::wstring& path) { g_rbLogPath = path; }

// ── Settings persistence ──────────────────────────────────────────────────────
void SaveReplaySettings(const ReplaySettings& s, const std::wstring& iniPath)
{
    const wchar_t* ini = iniPath.c_str();
    const wchar_t* sec = L"ReplayBuffer";
    wchar_t buf[32];

    swprintf_s(buf, L"%d", s.minutes);
    WritePrivateProfileStringW(sec, L"Minutes",      buf, ini);
    swprintf_s(buf, L"%d", s.fps);
    WritePrivateProfileStringW(sec, L"Fps",          buf, ini);
    swprintf_s(buf, L"%d", s.ramMB);
    WritePrivateProfileStringW(sec, L"RamMB",        buf, ini);
    swprintf_s(buf, L"%d", s.monitorIndex);
    WritePrivateProfileStringW(sec, L"MonitorIndex", buf, ini);
    WritePrivateProfileStringW(sec, L"SaveFolder",   s.saveFolder.c_str(), ini);
    swprintf_s(buf, L"%u", s.startStopVK);
    WritePrivateProfileStringW(sec, L"StartStopVK",   buf, ini);
    swprintf_s(buf, L"%u", s.startStopMods);
    WritePrivateProfileStringW(sec, L"StartStopMods", buf, ini);
    swprintf_s(buf, L"%u", s.saveVK);
    WritePrivateProfileStringW(sec, L"SaveVK",        buf, ini);
    swprintf_s(buf, L"%u", s.saveMods);
    WritePrivateProfileStringW(sec, L"SaveMods",      buf, ini);
    WritePrivateProfileStringW(sec, L"PromptSaveOnStop",
                               s.promptSaveOnStop ? L"1" : L"0", ini);
    WritePrivateProfileStringW(sec, L"AutoStartOnPlay",
                               s.autoStartOnPlay ? L"1" : L"0", ini);
}

ReplaySettings LoadReplaySettings(const std::wstring& iniPath)
{
    ReplaySettings s;
    const wchar_t* ini = iniPath.c_str();
    const wchar_t* sec = L"ReplayBuffer";
    wchar_t buf[MAX_PATH] = {};

    auto RdInt = [&](const wchar_t* key, int def) -> int {
        GetPrivateProfileStringW(sec, key, L"", buf, MAX_PATH, ini);
        return (buf[0] != L'\0') ? _wtoi(buf) : def;
    };
    auto RdUint = [&](const wchar_t* key) -> UINT {
        GetPrivateProfileStringW(sec, key, L"0", buf, MAX_PATH, ini);
        return (UINT)_wtol(buf);
    };

    s.minutes      = RdInt(L"Minutes",      2);
    s.fps          = RdInt(L"Fps",          30);
    s.ramMB        = RdInt(L"RamMB",        0);
    s.monitorIndex = RdInt(L"MonitorIndex", 0);

    GetPrivateProfileStringW(sec, L"SaveFolder", L"", buf, MAX_PATH, ini);
    s.saveFolder      = buf;
    s.startStopVK     = RdUint(L"StartStopVK");
    s.startStopMods   = RdUint(L"StartStopMods");
    s.saveVK            = RdUint(L"SaveVK");
    s.saveMods          = RdUint(L"SaveMods");
    s.promptSaveOnStop  = (RdInt(L"PromptSaveOnStop", 1) != 0);
    s.autoStartOnPlay   = (RdInt(L"AutoStartOnPlay",  0) != 0);

    if (s.minutes < 1)  s.minutes = 1;
    if (s.minutes > 60) s.minutes = 60;
    if (s.fps < 20)     s.fps = 20;
    if (s.fps > 60)     s.fps = 60;

    return s;
}

// ── Hotkey format helper ──────────────────────────────────────────────────────
std::wstring FormatHotkey(UINT vk, UINT mods)
{
    if (vk == 0) return L"(none)";
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl+";
    if (mods & MOD_SHIFT)   s += L"Shift+";
    if (mods & MOD_ALT)     s += L"Alt+";

    UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t keyName[64] = {};
    GetKeyNameTextW((LONG)(scanCode << 16), keyName, 64);
    if (keyName[0] == L'\0') swprintf_s(keyName, L"VK%u", vk);
    s += keyName;
    return s;
}
