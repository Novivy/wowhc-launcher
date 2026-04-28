#include "upload_window.h"
#include "google_drive.h"
#include "replay_buffer.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <propsys.h>
#include <propkey.h>

#include <string>
#include <vector>
#include <thread>
#include <algorithm>

#include <uxtheme.h>
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")

// ── Dark palette (mirrors main.cpp) ──────────────────────────────────────────
static const COLORREF UP_BG      = RGB(22,  22,  26);
static const COLORREF UP_BG2     = RGB(38,  38,  44);
static const COLORREF UP_TEXT    = RGB(210, 210, 215);
static const COLORREF UP_DIM     = RGB(100, 100, 110);
static const COLORREF UP_ACCENT  = RGB(185, 140, 25);
static const COLORREF UP_SEP     = RGB(55,  55,  62);

// ── Control IDs ───────────────────────────────────────────────────────────────
enum : UINT {
    UID_LISTBOX       = 401,
    UID_BTN_UPLOAD    = 402,
    UID_BTN_CLOSE     = 403,
    UID_LINK_FOLDER   = 404,
    UID_STATIC_STATUS = 405,
    UID_TIMER_THUMB   = 406,
    UID_PROGRESS      = 407,
    UID_LINK_DRIVE      = 408,
    UID_LINK_DISCONNECT = 409,
    UID_LABEL_NOT_CONN  = 410,
    UID_TIMER_HOVER     = 411,
};

// WM_APP messages local to this window
#define WM_UP_STATUS   (WM_APP + 50)   // lParam = new std::wstring*
#define WM_UP_PROGRESS (WM_APP + 51)   // wParam = 0-100
#define WM_UP_DONE     (WM_APP + 52)   // lParam = new GDriveResult*
#define WM_UP_THUMB    (WM_APP + 53)   // wParam = item index, lParam = HBITMAP

// ── Video item ────────────────────────────────────────────────────────────────
struct VideoItem {
    std::wstring path;
    std::wstring filename;
    std::wstring datetime;
    std::wstring duration;
    HBITMAP      thumb    = nullptr;
    DWORD64      fileSize = 0;
};

// ── Module state ──────────────────────────────────────────────────────────────
static HWND              g_upHwnd       = nullptr;
static HWND              g_upOwner      = nullptr;
static HWND              g_upListBox    = nullptr;
static HWND              g_upBtnUpload  = nullptr;
static HWND              g_upBtnClose   = nullptr;
static HWND              g_upLinkFolder = nullptr;
static HWND              g_upLinkDrive      = nullptr;
static HWND              g_upLinkDisconnect  = nullptr;
static HWND              g_upLabelNotConn    = nullptr;
static HWND              g_upStatus          = nullptr;
static HWND              g_upProgress   = nullptr;
static HFONT             g_upFontNorm   = nullptr;
static HFONT             g_upFontBold   = nullptr;
static HFONT             g_upFontSmall  = nullptr;
static HFONT             g_upFontLink   = nullptr;
static HBRUSH            g_upBrBg       = nullptr;
static HBRUSH            g_upBrBg2      = nullptr;
static UINT              g_upDpi        = 96;
static std::wstring      g_upConfigDir;
static bool              g_upUploading  = false;

static std::vector<VideoItem> g_upItems;
static int                    g_upThumbNext = 0; // next item index to load thumb for

static bool g_upUploadHover = false;
static bool g_upCloseHover  = false;
static bool g_upFolderHover = false;
static bool g_upDriveHover      = false;
static bool g_upDisconnectHover = false;

static float g_upUploadHoverT     = 0.0f;
static float g_upFolderHoverT     = 0.0f;
static float g_upDriveHoverT      = 0.0f;
static float g_upDisconnectHoverT = 0.0f;

// ── Helpers ───────────────────────────────────────────────────────────────────
static int DU(int px)
{
    return MulDiv(px, (int)g_upDpi, 96);
}

static std::wstring FormatDateTime(const FILETIME& ft)
{
    FILETIME local; FileTimeToLocalFileTime(&ft, &local);
    SYSTEMTIME st;  FileTimeToSystemTime(&local, &st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d-%02d-%02d  %02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

static std::wstring FormatSize(DWORD64 bytes)
{
    wchar_t buf[32];
    if (bytes >= 1024ULL*1024*1024)
        swprintf_s(buf, L"%.1f GB", bytes / (1024.0*1024.0*1024.0));
    else
        swprintf_s(buf, L"%.0f MB", bytes / (1024.0*1024.0));
    return buf;
}

static std::wstring GetVideoDuration(const std::wstring& path)
{
    IShellItem2* pItem = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr,
            IID_PPV_ARGS(&pItem)))) return {};
    ULONGLONG dur = 0;
    HRESULT hr = pItem->GetUInt64(PKEY_Media_Duration, &dur);
    pItem->Release();
    if (FAILED(hr) || dur == 0) return {};
    int totalSec = (int)(dur / 10000000ULL);
    int h = totalSec / 3600;
    int m = (totalSec % 3600) / 60;
    int s = totalSec % 60;
    wchar_t buf[32];
    if (h > 0) swprintf_s(buf, L"%d:%02d:%02d", h, m, s);
    else        swprintf_s(buf, L"%d:%02d", m, s);
    return buf;
}

static HBITMAP GetVideoThumb(const std::wstring& path, int w, int h)
{
    IShellItemImageFactory* pSIF = nullptr;
    HBITMAP hBmp = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr,
            IID_PPV_ARGS(&pSIF)))) {
        SIZE sz = { w, h };
        pSIF->GetImage(sz, SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT, &hBmp);
        pSIF->Release();
    }
    return hBmp;
}

// Scans the recorder save folder for MP4s, sorts newest first
static void ScanVideos()
{
    g_upItems.clear();
    std::wstring folder = RB_GetSettings().saveFolder;
    if (folder.empty()) return;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((folder + L"\\*.mp4").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        VideoItem item;
        item.path     = folder + L"\\" + fd.cFileName;
        item.filename = fd.cFileName;
        item.datetime = FormatDateTime(fd.ftLastWriteTime);
        item.fileSize = ((DWORD64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        g_upItems.push_back(std::move(item));
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    // Sort newest first by write time (embedded in datetime string sorts correctly)
    std::sort(g_upItems.begin(), g_upItems.end(),
        [](const VideoItem& a, const VideoItem& b) {
            return a.datetime > b.datetime;
        });
}

// Copy text to clipboard
static void CopyToClipboard(HWND hwnd, const std::wstring& text)
{
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        wchar_t* p = (wchar_t*)GlobalLock(hMem);
        if (p) { memcpy(p, text.c_str(), bytes); GlobalUnlock(hMem); }
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}

static void UpdateLinkVisibility()
{
    // HasToken loads the token file (restoring s_cachedFolderId) — must run before HasFolder
    bool hasToken = GDrive_HasToken(g_upConfigDir);
    bool hasFolder = GDrive_HasFolder();
    if (g_upLinkDrive)
        ShowWindow(g_upLinkDrive,       hasFolder  ? SW_SHOW : SW_HIDE);
    if (g_upLinkDisconnect)
        ShowWindow(g_upLinkDisconnect,  hasToken   ? SW_SHOW : SW_HIDE);
    if (g_upLabelNotConn)
        ShowWindow(g_upLabelNotConn,   !hasToken   ? SW_SHOW : SW_HIDE);
}

// ── Listbox cursor subclass ───────────────────────────────────────────────────
static LRESULT CALLBACK ListBoxCursorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                           UINT_PTR, DWORD_PTR)
{
    if (msg == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static COLORREF UpLerpColor(COLORREF a, COLORREF b, float t) {
    return RGB(
        (int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
        (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
        (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

// ── Hover subclass ─────────────────────────────────────────────────────────────
static LRESULT CALLBACK UpBtnSub(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR, DWORD_PTR data)
{
    bool* pHover = (bool*)data;
    if (msg == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
    if (msg == WM_MOUSEMOVE && !*pHover) {
        *pHover = true;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        SetTimer(GetParent(hwnd), UID_TIMER_HOVER, 16, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (msg == WM_MOUSELEAVE) {
        *pHover = false;
        SetTimer(GetParent(hwnd), UID_TIMER_HOVER, 16, nullptr);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ── Draw a dark button ────────────────────────────────────────────────────────
// isAccent=true → gold. isSecondary=true → dimmer dark (for lower-priority actions).
// t = smoothstepped hover value (0.0 idle, 1.0 fully hovered).
static void DrawUpButton(LPDRAWITEMSTRUCT dis, float t, bool isAccent,
                         bool isSecondary = false)
{
    RECT rc = dis->rcItem; HDC hdc = dis->hDC;
    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED)  != 0;

    COLORREF bg, border;
    if (isAccent) {
        bg     = pressed ? RGB(40, 33, 13) : UpLerpColor(RGB(58, 47, 18), RGB(80,  65,  25), t);
        border = pressed ? RGB(100,75, 12) : UpLerpColor(RGB(130,98, 16), RGB(160, 120, 20), t);
    } else if (isSecondary) {
        bg     = pressed ? RGB(30, 30, 36) : UpLerpColor(RGB(26, 26, 31), RGB(36,  36,  42), t);
        border = pressed ? RGB(55, 55, 62) : UpLerpColor(RGB(50, 50, 57), RGB(70,  70,  78), t);
    } else {
        bg     = pressed ? RGB(55, 55, 62) : UpLerpColor(RGB(45, 45, 52), RGB(58,  58,  66), t);
        border = pressed ? RGB(80, 80, 88) : UpLerpColor(RGB(80, 80, 88), RGB(100, 100, 110), t);
    }

    COLORREF fg = disabled ? UP_DIM : UP_TEXT;
    HBRUSH hbr  = CreateSolidBrush(bg);
    FillRect(hdc, &rc, hbr); DeleteObject(hbr);

    HPEN hpen = CreatePen(PS_SOLID, 1, border);
    HPEN old  = (HPEN)SelectObject(hdc, hpen);
    MoveToEx(hdc, rc.left,    rc.top,      nullptr);
    LineTo  (hdc, rc.right-1, rc.top);
    LineTo  (hdc, rc.right-1, rc.bottom-1);
    LineTo  (hdc, rc.left,    rc.bottom-1);
    LineTo  (hdc, rc.left,    rc.top);
    SelectObject(hdc, old); DeleteObject(hpen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    SelectObject(hdc, g_upFontBold);
    wchar_t txt[128] = {};
    GetWindowTextW(dis->hwndItem, txt, 128);
    DrawTextW(hdc, txt, -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// ── Draw a listbox item ───────────────────────────────────────────────────────
static void DrawVideoItem(LPDRAWITEMSTRUCT dis)
{
    if (dis->itemID == (UINT)-1) return;
    if (dis->itemID >= (UINT)g_upItems.size()) return;

    const VideoItem& item = g_upItems[dis->itemID];
    HDC  hdc = dis->hDC;
    RECT rc  = dis->rcItem;
    bool sel = (dis->itemState & ODS_SELECTED) != 0;

    // Background
    COLORREF bgClr = sel ? RGB(38, 42, 52) : UP_BG;
    HBRUSH hbr = CreateSolidBrush(bgClr);
    FillRect(hdc, &rc, hbr); DeleteObject(hbr);

    // Subtle left accent bar on selection
    if (sel) {
        HBRUSH ab = CreateSolidBrush(UP_ACCENT);
        RECT bar = { rc.left, rc.top, rc.left + DU(3), rc.bottom };
        FillRect(hdc, &bar, ab); DeleteObject(ab);
    }

    const int THUMB_W = DU(128);
    const int THUMB_H = rc.bottom - rc.top - DU(4);
    int tx = rc.left + DU(2);
    int ty = rc.top  + DU(2);
    RECT thumbRc = { tx, ty, tx + THUMB_W, ty + THUMB_H };

    // Thumbnail or dark placeholder
    if (item.thumb) {
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, item.thumb);
        BITMAP bm; GetObject(item.thumb, sizeof(bm), &bm);
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        StretchBlt(hdc, thumbRc.left, thumbRc.top, THUMB_W, THUMB_H,
                   hdcMem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
    } else {
        HBRUSH ph = CreateSolidBrush(RGB(35, 35, 42));
        FillRect(hdc, &thumbRc, ph); DeleteObject(ph);
    }

    // Play icon overlay — semi-transparent dark circle + anti-aliased ▶ glyph
    {
        int cx = (thumbRc.left + thumbRc.right) / 2;
        int cy = (thumbRc.top  + thumbRc.bottom) / 2;
        int r  = DU(16);
        int bw = r*2, bh = r*2;
        HDC     hdcP = CreateCompatibleDC(hdc);
        HBITMAP hbmP = CreateCompatibleBitmap(hdc, bw, bh);
        HBITMAP hbmO = (HBITMAP)SelectObject(hdcP, hbmP);
        // Dark circle
        HBRUSH cbr  = CreateSolidBrush(RGB(10,10,14));
        HPEN   cpen = CreatePen(PS_SOLID, 0, RGB(10,10,14));
        HPEN   ppen = (HPEN)SelectObject(hdcP, cpen);
        HBRUSH opBr = (HBRUSH)SelectObject(hdcP, cbr);
        RECT bgRc = {0, 0, bw, bh};
        FillRect(hdcP, &bgRc, cbr);
        Ellipse(hdcP, 0, 0, bw, bh);
        SelectObject(hdcP, ppen); DeleteObject(cpen);
        SelectObject(hdcP, opBr); DeleteObject(cbr);
        // ▶ glyph — anti-aliased, visually centered
        LOGFONTW lf = {};
        lf.lfHeight  = -MulDiv(13, (int)g_upDpi, 72);
        lf.lfWeight  = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = ANTIALIASED_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI Symbol");
        HFONT hPlayFont = CreateFontIndirectW(&lf);
        HFONT hOldFont  = (HFONT)SelectObject(hdcP, hPlayFont);
        SetBkMode(hdcP, TRANSPARENT);
        SetTextColor(hdcP, RGB(255,255,255));
        RECT glyphRc = {0, 0, bw, bh};
        DrawTextW(hdcP, L"▶", 1, &glyphRc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdcP, hOldFont);
        DeleteObject(hPlayFont);
        // Blend at 55% alpha
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 140, 0};
        AlphaBlend(hdc, cx - r, cy - r, bw, bh, hdcP, 0, 0, bw, bh, bf);
        SelectObject(hdcP, hbmO);
        DeleteObject(hbmP);
        DeleteDC(hdcP);
    }

    // Text area (right of thumbnail): filename / date+duration / size
    int textX = thumbRc.right + DU(10);
    int textW = rc.right - textX - DU(8);
    SetBkMode(hdc, TRANSPARENT);

    SetTextColor(hdc, UP_TEXT);
    SelectObject(hdc, g_upFontNorm);
    RECT nameRc = { textX, ty, textX + textW, ty + DU(18) };
    DrawTextW(hdc, item.filename.c_str(), -1, &nameRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(hdc, g_upFontSmall);
    SetTextColor(hdc, UP_DIM);
    if (!item.datetime.empty()) {
        RECT dtRc = { textX, ty + DU(22), textX + textW, ty + DU(36) };
        DrawTextW(hdc, item.datetime.c_str(), -1, &dtRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!item.duration.empty()) {
        RECT durRc = { textX, ty + DU(22), textX + textW, ty + DU(36) };
        DrawTextW(hdc, item.duration.c_str(), -1, &durRc,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (item.fileSize > 0) {
        std::wstring sz = FormatSize(item.fileSize);
        RECT szRc = { textX, ty + DU(38), textX + textW, ty + DU(52) };
        DrawTextW(hdc, sz.c_str(), -1, &szRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // Row separator
    HPEN lp = CreatePen(PS_SOLID, 1, UP_SEP);
    HPEN lo = (HPEN)SelectObject(hdc, lp);
    MoveToEx(hdc, rc.left,  rc.bottom-1, nullptr);
    LineTo  (hdc, rc.right, rc.bottom-1);
    SelectObject(hdc, lo); DeleteObject(lp);
}

// ── Result dialog (shareable link) ────────────────────────────────────────────
// Client layout (96 DPI): 20px padding, content width 364px
//   y=20  label h=18
//   y=44  edit  h=24
//   y=78  Copy Link button (accent) h=60
//   y=146 Create Death Appeal button (dark) h=60
//   y=226 bottom (146+60+20)
// Neither button closes the dialog — only the X button does.
static const int RD_W  = 404; // client width
static const int RD_H  = 226; // client height

static std::wstring s_resLinkStr; // share link, set in WM_CREATE, used for appeal URL
static bool   s_resDone      = false;
static HFONT  s_resFont      = nullptr;
static HFONT  s_resFontBold  = nullptr;
static HBRUSH s_resBrBg      = nullptr;
static HBRUSH s_resBrBg2     = nullptr;
static HWND   s_resBtnCopy   = nullptr;
static HWND   s_resBtnDrive  = nullptr;
static bool   s_resCopyHover  = false;
static bool   s_resDriveHover = false;
static float  s_resCopyHoverT  = 0.0f;
static float  s_resDriveHoverT = 0.0f;

static LRESULT CALLBACK ResultWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        auto* link = (const std::wstring*)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams;
        s_resLinkStr = *link;
        UINT dpi = GetDpiForWindow(hwnd);
        auto D  = [dpi](int px) { return MulDiv(px, (int)dpi, 96); };
        auto MF = [dpi](int pt, int wt) -> HFONT {
            LOGFONTW lf = {};
            lf.lfHeight  = -MulDiv(pt, (int)dpi, 72);
            lf.lfWeight  = wt;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            return CreateFontIndirectW(&lf);
        };
        s_resFont      = MF(9, FW_NORMAL);
        s_resFontBold  = MF(9, FW_SEMIBOLD);
        s_resBrBg      = CreateSolidBrush(UP_BG);
        s_resBrBg2     = CreateSolidBrush(UP_BG2);
        auto SF = [](HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); };

        HWND hLbl = CreateWindowExW(0, L"STATIC",
            L"Your video has been uploaded. Copy the link to share it:",
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            D(20), D(20), D(364), D(18), hwnd, nullptr, nullptr, nullptr);
        SF(hLbl, s_resFont);

        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", link->c_str(),
            WS_CHILD|WS_VISIBLE|ES_READONLY|ES_AUTOHSCROLL,
            D(20), D(44), D(364), D(24), hwnd,
            (HMENU)(UINT_PTR)1001, nullptr, nullptr);
        SF(hEdit, s_resFont);
        SendMessageW(hEdit, EM_SETSEL, 0, -1);

        s_resBtnCopy = CreateWindowExW(0, L"BUTTON", L"Copy Link",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            D(20), D(78), D(364), D(60), hwnd,
            (HMENU)(UINT_PTR)IDOK, nullptr, nullptr);
        SF(s_resBtnCopy, s_resFontBold);

        s_resBtnDrive = CreateWindowExW(0, L"BUTTON", L"Create Death Appeal",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            D(20), D(146), D(364), D(60), hwnd,
            (HMENU)(UINT_PTR)1002, nullptr, nullptr);
        SF(s_resBtnDrive, s_resFontBold);

        s_resCopyHover  = false;
        s_resDriveHover = false;
        SetWindowSubclass(s_resBtnCopy,  UpBtnSub, 20, (DWORD_PTR)&s_resCopyHover);
        SetWindowSubclass(s_resBtnDrive, UpBtnSub, 21, (DWORD_PTR)&s_resDriveHover);

        SetFocus(hEdit);
        return 0;
    }
    case WM_TIMER:
        if (wp == UID_TIMER_HOVER) {
            bool anyActive = false;
            auto step = [&](bool in, float& t, HWND btn) {
                float target = in ? 1.0f : 0.0f;
                float speed  = in ? 0.10f : 0.16f;
                if (fabsf(t - target) > 0.001f) {
                    t = in ? (t + speed < 1.0f ? t + speed : 1.0f) : (t - speed > 0.0f ? t - speed : 0.0f);
                    if (btn) InvalidateRect(btn, nullptr, FALSE);
                    anyActive = true;
                } else { t = target; }
            };
            step(s_resCopyHover,  s_resCopyHoverT,  s_resBtnCopy);
            step(s_resDriveHover, s_resDriveHoverT, s_resBtnDrive);
            if (!anyActive) KillTimer(hwnd, UID_TIMER_HOVER);
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        auto* dis = (LPDRAWITEMSTRUCT)lp;
        if (dis->hwndItem == s_resBtnCopy) {
            float t = s_resCopyHoverT;  t = t * t * (3.0f - 2.0f * t);
            DrawUpButton(dis, t, false, true);  return TRUE;
        }
        if (dis->hwndItem == s_resBtnDrive) {
            float t = s_resDriveHoverT; t = t * t * (3.0f - 2.0f * t);
            DrawUpButton(dis, t, false, false); return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[4096] = {};
            GetDlgItemTextW(hwnd, 1001, buf, 4096);
            CopyToClipboard(hwnd, buf);
            // Stay open — user closes with X
        } else if (LOWORD(wp) == 1002) {
            std::wstring url = L"https://wow-hc.com/appeal#video=" + s_resLinkStr;
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            // Stay open — user closes with X
        }
        break;
    case WM_CLOSE:
        s_resDone = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        s_resBtnCopy  = nullptr;
        s_resBtnDrive = nullptr;
        if (s_resFont)     { DeleteObject(s_resFont);     s_resFont     = nullptr; }
        if (s_resFontBold) { DeleteObject(s_resFontBold); s_resFontBold = nullptr; }
        if (s_resBrBg)     { DeleteObject(s_resBrBg);     s_resBrBg     = nullptr; }
        if (s_resBrBg2)    { DeleteObject(s_resBrBg2);    s_resBrBg2    = nullptr; }
        break;
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, s_resBrBg);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wp, UP_BG);
        SetTextColor((HDC)wp, UP_TEXT);
        return (LRESULT)s_resBrBg;
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wp, UP_BG2);
        SetTextColor((HDC)wp, UP_TEXT);
        return (LRESULT)s_resBrBg2;
    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        UINT dpi = GetDpiForWindow(hwnd);
        RECT rc = {0, 0, MulDiv(RD_W, (int)dpi, 96), MulDiv(RD_H, (int)dpi, 96)};
        AdjustWindowRectExForDpi(&rc, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE, 0, dpi);
        mmi->ptMinTrackSize = {rc.right-rc.left, rc.bottom-rc.top};
        mmi->ptMaxTrackSize = {rc.right-rc.left, rc.bottom-rc.top};
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowResultDialog(HWND parent, const std::wstring& link)
{
    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ResultWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"WOWHCResultDlg";
        RegisterClassExW(&wc);
        classReg = true;
    }
    UINT dpi = GetDpiForWindow(parent ? parent : GetDesktopWindow());
    RECT rc = {0, 0, MulDiv(RD_W, (int)dpi, 96), MulDiv(RD_H, (int)dpi, 96)};
    AdjustWindowRectExForDpi(&rc, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE, 0, dpi);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + (pr.right  - pr.left - w) / 2;
    int y = pr.top  + (pr.bottom - pr.top  - h) / 2;
    EnableWindow(parent, FALSE);
    s_resDone = false;
    HWND dlg = CreateWindowExW(0, L"WOWHCResultDlg", L"Upload Complete",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), (LPVOID)&link);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(dlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    if (parent) {
        HICON hSm  = (HICON)SendMessageW(parent, WM_GETICON, ICON_SMALL, 0);
        HICON hBig = (HICON)SendMessageW(parent, WM_GETICON, ICON_BIG,   0);
        if (!hSm)  hSm  = (HICON)GetClassLongPtrW(parent, GCLP_HICONSM);
        if (!hBig) hBig = (HICON)GetClassLongPtrW(parent, GCLP_HICON);
        if (hSm)  SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hSm);
        if (hBig) SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hBig);
    }
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    MSG m;
    while (!s_resDone && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &m)) {
            TranslateMessage(&m); DispatchMessageW(&m);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}

// ── Window procedure ──────────────────────────────────────────────────────────
static LRESULT CALLBACK UploadWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_CREATE: {
        g_upHwnd  = hwnd;
        g_upOwner = GetWindow(hwnd, GW_OWNER);
        g_upDpi   = GetDpiForWindow(hwnd);

        auto MkFont = [](int pt, int wt) -> HFONT {
            LOGFONTW lf = {};
            lf.lfHeight  = -MulDiv(pt, GetDpiForWindow(GetDesktopWindow()), 72);
            lf.lfWeight  = wt;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            return CreateFontIndirectW(&lf);
        };
        g_upFontNorm  = MkFont(9,  FW_NORMAL);
        g_upFontBold  = MkFont(9,  FW_SEMIBOLD);
        {
            LOGFONTW lf = {};
            lf.lfHeight    = -MulDiv(9, GetDpiForWindow(GetDesktopWindow()), 72);
            lf.lfWeight    = FW_NORMAL;
            lf.lfUnderline = TRUE;
            lf.lfCharSet   = DEFAULT_CHARSET;
            lf.lfQuality   = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            g_upFontLink = CreateFontIndirectW(&lf);
        }
        g_upFontSmall = MkFont(8,  FW_NORMAL);
        g_upBrBg      = CreateSolidBrush(UP_BG);
        g_upBrBg2     = CreateSolidBrush(UP_BG2);

        auto D  = [](int px) { return MulDiv(px, (int)g_upDpi, 96); };
        auto SF = [](HWND h, HFONT f) {
            SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
        };
        auto Lbl = [&](const wchar_t* t, int x, int y, int w, int h) -> HWND {
            HWND hl = CreateWindowExW(0, L"STATIC", t,
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                D(x), D(y), D(w), D(h), hwnd, nullptr, nullptr, nullptr);
            SF(hl, g_upFontNorm);
            return hl;
        };

        // Intro
        Lbl(L"Upload your recordings with Google Drive.", 14, 22, 490, 28);

        // Video list (owner-draw fixed listbox)
        g_upListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            D(14), D(56), D(490), D(272),
            hwnd, (HMENU)(UINT_PTR)UID_LISTBOX, nullptr, nullptr);
        SF(g_upListBox, g_upFontNorm);
        // AllowDarkModeForWindow (uxtheme ordinal 133, stable since Win10 1809)
        // required so DarkMode_Explorer renders scrollbar arrows with proper contrast
        using FnAllow = BOOL(WINAPI*)(HWND, BOOL);
        static auto fnAllow = (FnAllow)GetProcAddress(
            GetModuleHandleW(L"uxtheme.dll"), MAKEINTRESOURCEA(133));
        if (fnAllow) fnAllow(g_upListBox, TRUE);
        SetWindowTheme(g_upListBox, L"DarkMode_Explorer", nullptr);
        SetWindowSubclass(g_upListBox, ListBoxCursorProc, 10, 0);

        // Populate list
        ScanVideos();
        for (int i = 0; i < (int)g_upItems.size(); i++) {
            SendMessageW(g_upListBox, LB_ADDSTRING, 0,
                         (LPARAM)g_upItems[i].filename.c_str());
            g_upItems[i].duration = GetVideoDuration(g_upItems[i].path);
        }
        if (!g_upItems.empty())
            SendMessageW(g_upListBox, LB_SETCURSEL, 0, 0);

        // "Open local Videos folder" link (left) + "Open Google Drive folder" link (right)
        g_upLinkFolder = CreateWindowExW(0, L"STATIC", L"Open local Videos folder",
            WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_LEFT,
            D(14), D(340), D(230), D(18),
            hwnd, (HMENU)(UINT_PTR)UID_LINK_FOLDER, nullptr, nullptr);
        SF(g_upLinkFolder, g_upFontLink);

        g_upLinkDrive = CreateWindowExW(0, L"STATIC", L"Open Google Drive folder",
            WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_RIGHT,
            D(274), D(340), D(230), D(18),
            hwnd, (HMENU)(UINT_PTR)UID_LINK_DRIVE, nullptr, nullptr);
        SF(g_upLinkDrive, g_upFontLink);

        // Status text — 20px below links row (opaque background prevents overlap)
        g_upStatus = CreateWindowExW(0, L"STATIC", L"Ready to upload on Google Drive",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            D(14), D(378), D(490), D(18),
            hwnd, (HMENU)(UINT_PTR)UID_STATIC_STATUS, nullptr, nullptr);
        SF(g_upStatus, g_upFontNorm);

        // Progress bar
        g_upProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            D(14), D(400), D(490), D(14),
            hwnd, (HMENU)(UINT_PTR)UID_PROGRESS, nullptr, nullptr);
        SendMessageW(g_upProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(g_upProgress, PBM_SETPOS, 0, 0);
        SendMessageW(g_upProgress, PBM_SETBKCOLOR, 0, (LPARAM)UP_BG2);
        SendMessageW(g_upProgress, PBM_SETBARCOLOR, 0, (LPARAM)RGB(30, 100, 210));

        // Upload button — 20px below progress bar, double height
        g_upBtnUpload = CreateWindowExW(0, L"BUTTON", L"Upload with Google Drive",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            D(14), D(434), D(490), D(72),
            hwnd, (HMENU)(UINT_PTR)UID_BTN_UPLOAD, nullptr, nullptr);
        SF(g_upBtnUpload, g_upFontBold);

        // "Not connected" label — same slot as Disconnect link, shown when no token
        g_upLabelNotConn = CreateWindowExW(0, L"STATIC", L"Not Connected to Google Drive. Click Upload to connect.",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            D(14), D(516), D(490), D(18),
            hwnd, (HMENU)(UINT_PTR)UID_LABEL_NOT_CONN, nullptr, nullptr);
        SF(g_upLabelNotConn, g_upFontSmall);

        // Disconnect Google Drive link — centered below upload button
        g_upLinkDisconnect = CreateWindowExW(0, L"STATIC", L"Disconnect Google Drive",
            WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
            D(14), D(516), D(490), D(18),
            hwnd, (HMENU)(UINT_PTR)UID_LINK_DISCONNECT, nullptr, nullptr);
        SF(g_upLinkDisconnect, g_upFontLink);

        // Hover subclasses
        SetWindowSubclass(g_upBtnUpload,      UpBtnSub, 1, (DWORD_PTR)&g_upUploadHover);
        SetWindowSubclass(g_upLinkFolder,     UpBtnSub, 3, (DWORD_PTR)&g_upFolderHover);
        SetWindowSubclass(g_upLinkDrive,      UpBtnSub, 4, (DWORD_PTR)&g_upDriveHover);
        SetWindowSubclass(g_upLinkDisconnect, UpBtnSub, 5, (DWORD_PTR)&g_upDisconnectHover);

        UpdateLinkVisibility();

        // Start thumbnail loader timer (processes one thumb per tick)
        g_upThumbNext = 0;
        SetTimer(hwnd, UID_TIMER_THUMB, 80, nullptr);
        break;
    }

    case WM_TIMER:
        if (wp == UID_TIMER_HOVER) {
            bool anyActive = false;
            auto step = [&](bool in, float& t, HWND btn) {
                float target = in ? 1.0f : 0.0f;
                float speed  = in ? 0.10f : 0.16f;
                if (fabsf(t - target) > 0.001f) {
                    t = in ? (t + speed < 1.0f ? t + speed : 1.0f) : (t - speed > 0.0f ? t - speed : 0.0f);
                    if (btn) InvalidateRect(btn, nullptr, FALSE);
                    anyActive = true;
                } else { t = target; }
            };
            step(g_upUploadHover,     g_upUploadHoverT,     g_upBtnUpload);
            step(g_upFolderHover,     g_upFolderHoverT,     g_upLinkFolder);
            step(g_upDriveHover,      g_upDriveHoverT,      g_upLinkDrive);
            step(g_upDisconnectHover, g_upDisconnectHoverT, g_upLinkDisconnect);
            if (!anyActive) KillTimer(hwnd, UID_TIMER_HOVER);
            return 0;
        }
        if (wp == UID_TIMER_THUMB) {
            if (g_upThumbNext < (int)g_upItems.size()) {
                int i = g_upThumbNext++;
                // Load on a tiny thread to avoid blocking the timer
                std::thread([i, hwnd]() {
                    HBITMAP bmp = GetVideoThumb(g_upItems[i].path,
                                                128, 72);
                    PostMessageW(hwnd, WM_UP_THUMB, (WPARAM)i, (LPARAM)bmp);
                }).detach();
            } else {
                KillTimer(hwnd, UID_TIMER_THUMB);
            }
        }
        return 0;

    case WM_UP_THUMB: {
        int     idx = (int)wp;
        HBITMAP bmp = (HBITMAP)lp;
        if (idx >= 0 && idx < (int)g_upItems.size()) {
            if (g_upItems[idx].thumb) DeleteObject(g_upItems[idx].thumb);
            g_upItems[idx].thumb = bmp;
            RECT itemRc;
            if (SendMessageW(g_upListBox, LB_GETITEMRECT,
                             (WPARAM)idx, (LPARAM)&itemRc) != LB_ERR)
                InvalidateRect(g_upListBox, &itemRc, FALSE);
        }
        return 0;
    }

    case WM_UP_STATUS: {
        auto* ws = (std::wstring*)lp;
        if (ws) {
            SetWindowTextW(g_upStatus, ws->c_str());
            delete ws;
        }
        return 0;
    }

    case WM_UP_PROGRESS: {
        int pct = (int)wp;
        wchar_t buf[32];
        swprintf_s(buf, L"Uploading... %d%%", pct);
        SetWindowTextW(g_upStatus, buf);
        SendMessageW(g_upProgress, PBM_SETPOS, (WPARAM)pct, 0);
        return 0;
    }

    case WM_UP_DONE: {
        g_upUploading = false;
        EnableWindow(g_upBtnUpload, TRUE);
        SendMessageW(g_upProgress, PBM_SETPOS, 0, 0);
        auto* result = (GDriveResult*)lp;
        if (result) {
            if (result->error == GDriveError::None) {
                SetWindowTextW(g_upStatus, L"Upload complete!");
                UpdateLinkVisibility();
                ShowResultDialog(hwnd, result->shareLink);
            } else {
                SetWindowTextW(g_upStatus, result->errorMsg.c_str());
                MessageBoxW(hwnd, result->errorMsg.c_str(),
                            L"Upload Failed", MB_OK | MB_ICONERROR);
            }
            delete result;
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wp);
        WORD code = HIWORD(wp);

        if (id == UID_BTN_UPLOAD && code == BN_CLICKED) {
            if (g_upUploading) break;
            int sel = (int)SendMessageW(g_upListBox, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)g_upItems.size()) {
                MessageBoxW(hwnd, L"Please select a video to upload.",
                            L"No Video Selected", MB_OK | MB_ICONINFORMATION);
                break;
            }

            const VideoItem& item = g_upItems[sel];

            // Warn on large files
            if (item.fileSize > 1024ULL*1024*1024) {
                std::wstring msg =
                    L"This video is " + FormatSize(item.fileSize) +
                    L" and will use that amount of your Google Drive quota "
                    L"(15 GB free on a new account).\r\n\r\nContinue?";
                if (MessageBoxW(hwnd, msg.c_str(), L"Large File",
                                MB_YESNO | MB_ICONWARNING) != IDYES)
                    break;
            }

            if (!GDrive_HasClientId()) {
                MessageBoxW(hwnd,
                    L"Google Drive upload is not configured in this build.\r\n"
                    L"Contact the launcher author.",
                    L"Not Configured", MB_OK | MB_ICONWARNING);
                break;
            }

            g_upUploading = true;
            EnableWindow(g_upBtnUpload, FALSE);
            SetWindowTextW(g_upStatus, L"Connecting...");

            std::wstring path      = item.path;
            std::wstring configDir = g_upConfigDir;

            std::thread([hwnd, path, configDir]() {
                auto* result = new GDriveResult;
                *result = GDrive_Upload(
                    path, configDir,
                    [hwnd](DWORD64 done, DWORD64 total) {
                        if (total > 0)
                            PostMessageW(hwnd, WM_UP_PROGRESS,
                                (WPARAM)(done * 100 / total), 0);
                    },
                    [hwnd](const std::wstring& msg) {
                        PostMessageW(hwnd, WM_UP_STATUS, 0,
                            (LPARAM)(new std::wstring(msg)));
                    });
                PostMessageW(hwnd, WM_UP_DONE, 0, (LPARAM)result);
            }).detach();
            break;
        }

        if (id == UID_LINK_FOLDER && code == STN_CLICKED) {
            std::wstring folder = RB_GetSettings().saveFolder;
            if (!folder.empty())
                ShellExecuteW(nullptr, L"explore", folder.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }

        if (id == UID_LINK_DRIVE && code == STN_CLICKED) {
            std::wstring url = GDrive_GetFolderUrl();
            ShellExecuteW(nullptr, L"open", url.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }

        if (id == UID_LINK_DISCONNECT && code == STN_CLICKED) {
            if (MessageBoxW(hwnd,
                    L"Disconnect your Google Drive account from this launcher?\r\n"
                    L"Your uploaded videos will remain in your Drive.",
                    L"Disconnect Google Drive",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
                GDrive_Disconnect(g_upConfigDir);
                SetWindowTextW(g_upStatus, L"Google Drive disconnected.");
                UpdateLinkVisibility();
            }
            break;
        }

        if (id == UID_LISTBOX && code == LBN_SELCHANGE) {
            InvalidateRect(g_upListBox, nullptr, FALSE);
            break;
        }

        if (id == UID_LISTBOX && code == LBN_DBLCLK) {
            int sel = (int)SendMessageW(g_upListBox, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_upItems.size())
                ShellExecuteW(nullptr, L"open", g_upItems[sel].path.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        break;
    }

    case WM_MEASUREITEM: {
        auto* mis = (MEASUREITEMSTRUCT*)lp;
        if (mis->CtlID == UID_LISTBOX)
            mis->itemHeight = (UINT)DU(76);
        return TRUE;
    }

    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlID == UID_LISTBOX) {
            DrawVideoItem(dis);
            return TRUE;
        }
        if (dis->hwndItem == g_upBtnUpload) {
            float t = g_upUploadHoverT; t = t * t * (3.0f - 2.0f * t);
            DrawUpButton(dis, t, false);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC  hdc = (HDC)wp;
        HWND ctl = (HWND)lp;
        SetBkColor(hdc, UP_BG);
        if (ctl == g_upLinkFolder || ctl == g_upLinkDrive) {
            float raw = (ctl == g_upLinkFolder) ? g_upFolderHoverT : g_upDriveHoverT;
            float t = raw * raw * (3.0f - 2.0f * raw);
            SetTextColor(hdc, UpLerpColor(RGB(100,170,240), RGB(140,200,255), t));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (ctl == g_upLinkDisconnect) {
            float t = g_upDisconnectHoverT; t = t * t * (3.0f - 2.0f * t);
            SetTextColor(hdc, UpLerpColor(RGB(160,65,60), RGB(210,90,80), t));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (ctl == g_upLabelNotConn) {
            SetTextColor(hdc, UP_DIM);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (ctl == g_upStatus) {
            SetTextColor(hdc, UP_DIM);
            SetBkColor(hdc, UP_BG);
            SetBkMode(hdc, OPAQUE);
            return (LRESULT)g_upBrBg;
        }
        SetTextColor(hdc, UP_TEXT);
        return (LRESULT)g_upBrBg;
    }

    case WM_CTLCOLORLISTBOX: {
        SetBkColor((HDC)wp, UP_BG);
        SetTextColor((HDC)wp, UP_TEXT);
        return (LRESULT)g_upBrBg;
    }

    case WM_CTLCOLORBTN:
        SetBkColor((HDC)wp, UP_BG);
        return (LRESULT)g_upBrBg;

    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_upBrBg);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        // Separator above "Open folder"
        UINT dpi = GetDpiForWindow(hwnd);
        int  sy  = MulDiv(332, dpi, 96);
        HPEN sp  = CreatePen(PS_SOLID, 1, UP_SEP);
        HPEN op  = (HPEN)SelectObject(hdc, sp);
        RECT cr; GetClientRect(hwnd, &cr);
        MoveToEx(hdc, MulDiv(14, dpi, 96), sy, nullptr);
        LineTo  (hdc, cr.right - MulDiv(14, dpi, 96), sy);
        SelectObject(hdc, op); DeleteObject(sp);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        UINT dpi = GetDpiForWindow(hwnd);
        // Client: 518 wide (14+490+14), 548 tall (controls bottom 534 + 14 pad)
        RECT rc = {0, 0, MulDiv(518, (int)dpi, 96), MulDiv(548, (int)dpi, 96)};
        AdjustWindowRectExForDpi(&rc, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE, 0, dpi);
        mmi->ptMinTrackSize = {rc.right-rc.left, rc.bottom-rc.top};
        mmi->ptMaxTrackSize = {rc.right-rc.left, rc.bottom-rc.top};
        break;
    }

    case WM_CLOSE:
        KillTimer(hwnd, UID_TIMER_THUMB);
        // Free thumbnails
        for (auto& item : g_upItems)
            if (item.thumb) { DeleteObject(item.thumb); item.thumb = nullptr; }
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, UID_TIMER_THUMB);
        for (auto& item : g_upItems)
            if (item.thumb) { DeleteObject(item.thumb); item.thumb = nullptr; }
        g_upProgress       = nullptr;
        g_upLinkDrive      = nullptr;
        g_upLinkDisconnect = nullptr;
        g_upLabelNotConn   = nullptr;
        if (g_upFontNorm)  { DeleteObject(g_upFontNorm);  g_upFontNorm  = nullptr; }
        if (g_upFontBold)  { DeleteObject(g_upFontBold);  g_upFontBold  = nullptr; }
        if (g_upFontSmall) { DeleteObject(g_upFontSmall); g_upFontSmall = nullptr; }
        if (g_upFontLink)  { DeleteObject(g_upFontLink);  g_upFontLink  = nullptr; }
        if (g_upBrBg)      { DeleteObject(g_upBrBg);      g_upBrBg      = nullptr; }
        if (g_upBrBg2)     { DeleteObject(g_upBrBg2);     g_upBrBg2     = nullptr; }
        g_upHwnd = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Public API ─────────────────────────────────────────────────────────────────
void ShowUploadWindow(HWND hParent, const std::wstring& configDir)
{
    g_upConfigDir = configDir;

    if (g_upHwnd) {
        // Refresh video list each time window is shown
        ScanVideos();
        SendMessageW(g_upListBox, LB_RESETCONTENT, 0, 0);
        for (auto& item : g_upItems)
            SendMessageW(g_upListBox, LB_ADDSTRING, 0,
                         (LPARAM)item.filename.c_str());
        if (!g_upItems.empty())
            SendMessageW(g_upListBox, LB_SETCURSEL, 0, 0);
        g_upThumbNext = 0;
        SetTimer(g_upHwnd, UID_TIMER_THUMB, 80, nullptr);
        UpdateLinkVisibility();
        SetForegroundWindow(g_upHwnd);
        ShowWindow(g_upHwnd, SW_SHOW);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = UploadWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"WOWHCUploadWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    UINT dpi = GetDpiForWindow(hParent ? hParent : GetDesktopWindow());
    RECT adjRc = {0, 0, MulDiv(518, (int)dpi, 96), MulDiv(548, (int)dpi, 96)};
    AdjustWindowRectExForDpi(&adjRc, WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, FALSE, 0, dpi);
    int w = adjRc.right - adjRc.left;
    int h = adjRc.bottom - adjRc.top;

    RECT pr; GetWindowRect(hParent, &pr);
    int x = pr.left + (pr.right  - pr.left - w) / 2 + MulDiv(80, dpi, 96);
    int y = pr.top  + (pr.bottom - pr.top  - h) / 2;

    HWND hwnd = CreateWindowExW(0,
        L"WOWHCUploadWindow", L"Upload Replays",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h,
        hParent, nullptr, GetModuleHandleW(nullptr), nullptr);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    if (hParent) {
        HICON hSm  = (HICON)SendMessageW(hParent, WM_GETICON, ICON_SMALL, 0);
        HICON hBig = (HICON)SendMessageW(hParent, WM_GETICON, ICON_BIG,   0);
        if (!hSm)  hSm  = (HICON)GetClassLongPtrW(hParent, GCLP_HICONSM);
        if (!hBig) hBig = (HICON)GetClassLongPtrW(hParent, GCLP_HICON);
        if (hSm)  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSm);
        if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hBig);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

void CloseUploadWindow()
{
    if (g_upHwnd) SendMessageW(g_upHwnd, WM_CLOSE, 0, 0);
}
