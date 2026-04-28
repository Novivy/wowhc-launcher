#include "replay_ui.h"
#include "replay_buffer.h"

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shobjidl.h>

#include <string>
#include <vector>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

// ── Dark palette (mirrors main.cpp) ──────────────────────────────────────────
static const COLORREF RUI_BG      = RGB(22,  22,  26);
static const COLORREF RUI_BG2     = RGB(38,  38,  44);
static const COLORREF RUI_TEXT    = RGB(210, 210, 215);
static const COLORREF RUI_DIM     = RGB(100, 100, 110);
static const COLORREF RUI_SEP     = RGB(55,  55,  62);
static const COLORREF RUI_GREEN   = RGB(60,  180, 90);
static const COLORREF RUI_RED     = RGB(200, 60,  60);

// ── Control IDs ───────────────────────────────────────────────────────────────
enum : UINT {
    RID_COMBO_MONITOR  = 301,
    RID_EDIT_MINUTES   = 302,
    RID_EDIT_FPS            = 313,
    RID_CHECK_PROMPT_SAVE   = 314,
    RID_CHECK_AUTOSTART     = 315,
    RID_EDIT_FOLDER    = 305,
    RID_BTN_FOLDER     = 306,
    RID_EDIT_HK_START  = 307,
    RID_EDIT_HK_SAVE   = 308,
    RID_BTN_STARTSTOP  = 309,
    RID_BTN_SAVENOW    = 310,
    RID_STATIC_STATUS  = 311,
    RID_TIMER_POLL     = 312,
    RID_TIMER_HOVER    = 313,
    RID_BTN_CLOSE      = 316,
};

// ── Module state ──────────────────────────────────────────────────────────────
static HWND g_ruiHwnd         = nullptr;
static HWND g_ruiOwner        = nullptr; // main window (WS_OVERLAPPED owner, not retrievable via GetParent)
static HWND g_ruiComboMonitor = nullptr;
static HWND g_ruiEditMinutes  = nullptr;
static HWND g_ruiEditFps      = nullptr;
static HWND g_ruiEditFolder      = nullptr;
static HWND g_ruiBtnFolder       = nullptr;
static HWND g_ruiCheckPromptSave  = nullptr;
static HWND g_ruiCheckAutoStart   = nullptr;
static HWND g_ruiEditHkStart     = nullptr;
static HWND g_ruiEditHkSave   = nullptr;
static HWND g_ruiBtnStartStop = nullptr;
static HWND g_ruiBtnSave      = nullptr;
static HWND g_ruiBtnClose     = nullptr;
static HWND g_ruiStatus       = nullptr;

static HFONT g_ruiFontNorm  = nullptr;
static HFONT g_ruiFontBold  = nullptr;
static HFONT g_ruiFontSmall = nullptr;
static HBRUSH g_ruiBrBg     = nullptr;
static HBRUSH g_ruiBrBg2    = nullptr;
static UINT   g_ruiCurrentDpi = 96;

static bool g_ruiHkStartHover  = false;
static bool g_ruiHkSaveHover   = false;
static bool g_ruiBtnSSHover    = false;
static bool g_ruiBtnSvHover    = false;
static bool g_ruiBtnCloseHover = false;
static bool g_ruiBtnFolderHover= false;

static float g_ruiBtnSSHoverT     = 0.0f;
static float g_ruiBtnSvHoverT     = 0.0f;
static float g_ruiBtnCloseHoverT  = 0.0f;
static float g_ruiBtnFolderHoverT = 0.0f;

// Hotkey capture state
struct HotkeyState { UINT vk; UINT mods; bool capturing; };
static HotkeyState g_hkStart = {}, g_hkSave = {};

// ── Helpers ───────────────────────────────────────────────────────────────────
static int DU(int px)
{
    UINT dpi = GetDpiForWindow(g_ruiHwnd ? g_ruiHwnd : GetDesktopWindow());
    return MulDiv(px, dpi, 96);
}

static void RuiSetText(HWND h, const std::wstring& s)
{
    SetWindowTextW(h, s.c_str());
}

static void UpdateHotkeyEdit(HWND edit, const HotkeyState& hk)
{
    RuiSetText(edit, FormatHotkey(hk.vk, hk.mods));
}

static std::wstring GetEditText(HWND h)
{
    wchar_t buf[MAX_PATH] = {};
    GetWindowTextW(h, buf, MAX_PATH);
    return buf;
}

static int GetEditInt(HWND h, int def)
{
    auto s = GetEditText(h);
    if (s.empty()) return def;
    int v = _wtoi(s.c_str());
    return (v > 0) ? v : def;
}

static LRESULT CALLBACK HkEditSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR uId, DWORD_PTR data)
{
    HotkeyState* hk = reinterpret_cast<HotkeyState*>(data);

    if (msg == WM_SETFOCUS) {
        hk->capturing = true;
        RuiSetText(hwnd, L"(press a key...)");
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (msg == WM_KILLFOCUS) {
        hk->capturing = false;
        UpdateHotkeyEdit(hwnd, *hk);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (msg == WM_KEYDOWN && hk->capturing) {
        UINT vk = (UINT)wp;
        // Modifier keys alone — don't end capture, user may be building a combo
        static const UINT kModKeys[] = {
            VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
            VK_SHIFT,   VK_LSHIFT,   VK_RSHIFT,
            VK_MENU,    VK_LMENU,    VK_RMENU,
            VK_LWIN,    VK_RWIN, 0
        };
        for (int i = 0; kModKeys[i]; i++)
            if (vk == kModKeys[i]) return 0;

        if (vk == VK_ESCAPE) {
            hk->vk = 0; hk->mods = 0;
        } else {
            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= MOD_SHIFT;
            if (GetKeyState(VK_MENU)    & 0x8000) mods |= MOD_ALT;
            hk->vk = vk; hk->mods = mods;
        }
        UpdateHotkeyEdit(hwnd, *hk);
        hk->capturing = false;

        if (hk->vk != 0 && g_hkStart.vk == g_hkSave.vk && g_hkStart.mods == g_hkSave.mods) {
            hk->vk = 0; hk->mods = 0;
            UpdateHotkeyEdit(hwnd, *hk);
            SetFocus(GetParent(hwnd));
            MessageBoxW(GetParent(hwnd),
                L"Both hotkeys cannot be the same.\nThis hotkey has been cleared — please choose a different key.",
                L"Duplicate Hotkey", MB_OK | MB_ICONWARNING);
            return 0;
        }

        SetFocus(GetParent(hwnd));
        return 0;
    }
    if (msg == WM_CHAR) return 0; // suppress character beeps
    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static COLORREF RuiLerpColor(COLORREF a, COLORREF b, float t) {
    return RGB(
        (int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
        (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
        (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

static LRESULT CALLBACK BtnSubRui(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                   UINT_PTR uId, DWORD_PTR data)
{
    bool* pHover = reinterpret_cast<bool*>(data);
    if (msg == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
    if (msg == WM_MOUSEMOVE && !*pHover) {
        *pHover = true;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        SetTimer(GetParent(hwnd), RID_TIMER_HOVER, 16, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (msg == WM_MOUSELEAVE) {
        *pHover = false;
        SetTimer(GetParent(hwnd), RID_TIMER_HOVER, 16, nullptr);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// t = smoothstepped hover value (0.0 idle, 1.0 fully hovered).
static void DrawDarkButton(LPDRAWITEMSTRUCT dis, float t, bool isAccent, bool isSecondary = false)
{
    RECT rc = dis->rcItem;
    HDC  hdc = dis->hDC;
    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    COLORREF bg, border;
    if (isSecondary) {
        bg     = pressed ? RGB(30, 30, 36) : RuiLerpColor(RGB(26, 26, 31), RGB(36, 36, 42), t);
        border = pressed ? RGB(55, 55, 62) : RuiLerpColor(RGB(50, 50, 57), RGB(70, 70, 78), t);
    } else {
        bg     = pressed ? RGB(55, 55, 62)
                         : RuiLerpColor(RGB(45, 45, 52), isAccent ? RGB(68, 58, 30) : RGB(58, 58, 66), t);
        border = pressed ? RGB(80, 80, 88)
                         : RuiLerpColor(RGB(80, 80, 88), isAccent ? RGB(140, 105, 20) : RGB(100, 100, 110), t);
    }
    COLORREF fg = disabled ? RUI_DIM : RUI_TEXT;

    HBRUSH hbr = CreateSolidBrush(bg);
    FillRect(hdc, &rc, hbr);
    DeleteObject(hbr);

    HPEN hpen = CreatePen(PS_SOLID, 1, border);
    HPEN old  = (HPEN)SelectObject(hdc, hpen);
    MoveToEx(hdc, rc.left, rc.top, nullptr);
    LineTo(hdc, rc.right-1, rc.top);
    LineTo(hdc, rc.right-1, rc.bottom-1);
    LineTo(hdc, rc.left, rc.bottom-1);
    LineTo(hdc, rc.left, rc.top);
    SelectObject(hdc, old); DeleteObject(hpen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    SelectObject(hdc, g_ruiFontNorm);
    wchar_t txt[128] = {};
    GetWindowTextW(dis->hwndItem, txt, 128);
    DrawTextW(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// ── Hotkey registration with UI feedback ──────────────────────────────────────
static void RegisterHotkeysWithFeedback(HWND hwnd)
{
    int failed = RB_RegisterHotkeys();
    if (failed & RB_HK_STARTSTOP_FAILED) {
        g_hkStart = { 0, 0, false };
        UpdateHotkeyEdit(g_ruiEditHkStart, g_hkStart);
        MessageBoxW(hwnd,
            L"The Start/Stop hotkey is already in use by another application.\n"
            L"The hotkey has been cleared. Please choose a different key.",
            L"Hotkey Conflict", MB_OK | MB_ICONWARNING);
        SetFocus(hwnd);
    }
    if (failed & RB_HK_SAVE_FAILED) {
        g_hkSave = { 0, 0, false };
        UpdateHotkeyEdit(g_ruiEditHkSave, g_hkSave);
        MessageBoxW(hwnd,
            L"The Save Replay hotkey is already in use by another application.\n"
            L"The hotkey has been cleared. Please choose a different key.",
            L"Hotkey Conflict", MB_OK | MB_ICONWARNING);
        SetFocus(hwnd);
    }
}

// ── Apply settings from UI → ReplaySettings ───────────────────────────────────
static ReplaySettings CollectSettings()
{
    ReplaySettings s;
    s.monitorIndex   = (int)SendMessageW(g_ruiComboMonitor, CB_GETCURSEL, 0, 0);
    s.minutes        = GetEditInt(g_ruiEditMinutes, 2);
    if (s.minutes < 1) s.minutes = 1;
    if (s.minutes > 60) s.minutes = 60;
    s.fps            = GetEditInt(g_ruiEditFps, 30);
    if (s.fps < 20) s.fps = 20;
    if (s.fps > 60) s.fps = 60;
    s.promptSaveOnStop = (SendMessageW(g_ruiCheckPromptSave, BM_GETCHECK, 0, 0) == BST_CHECKED);
    s.autoStartOnPlay  = (SendMessageW(g_ruiCheckAutoStart,  BM_GETCHECK, 0, 0) == BST_CHECKED);
    s.ramMB        = 0;
    s.saveFolder    = GetEditText(g_ruiEditFolder);
    s.startStopVK   = g_hkStart.vk;
    s.startStopMods = g_hkStart.mods;
    s.saveVK        = g_hkSave.vk;
    s.saveMods      = g_hkSave.mods;
    return s;
}

static void RefreshStatus()
{
    if (!g_ruiHwnd) return;
    bool running = RB_IsRunning();
    SetWindowTextW(g_ruiBtnStartStop, running ? L"Stop Recording" : L"Start Recording");
    InvalidateRect(g_ruiBtnStartStop, nullptr, FALSE);
    if (g_ruiBtnSave) EnableWindow(g_ruiBtnSave, running ? TRUE : FALSE);

    if (g_ruiStatus) {
        SetWindowTextW(g_ruiStatus, running ? L"● Recording..." : L"■ Recording Stopped");
        RECT rc; GetWindowRect(g_ruiStatus, &rc);
        MapWindowPoints(HWND_DESKTOP, g_ruiHwnd, (POINT*)&rc, 2);
        InvalidateRect(g_ruiHwnd, &rc, TRUE);
    }
}

// ── Window procedure ──────────────────────────────────────────────────────────
static LRESULT CALLBACK ReplayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_ruiHwnd  = hwnd;
        g_ruiOwner = GetWindow(hwnd, GW_OWNER);

        g_ruiCurrentDpi = GetDpiForWindow(hwnd);
        auto MkFont = [](int pt, int w) -> HFONT {
            LOGFONTW lf = {};
            lf.lfHeight  = -MulDiv(pt, g_ruiCurrentDpi, 72);
            lf.lfWeight  = w;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            return CreateFontIndirectW(&lf);
        };
        g_ruiFontNorm  = MkFont(9,  FW_NORMAL);
        g_ruiFontBold  = MkFont(9,  FW_SEMIBOLD);
        g_ruiFontSmall = MkFont(8,  FW_NORMAL);
        g_ruiBrBg  = CreateSolidBrush(RUI_BG);
        g_ruiBrBg2 = CreateSolidBrush(RUI_BG2);

        auto D   = [](int px) { return MulDiv(px, g_ruiCurrentDpi, 96); };
        auto SF  = [](HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); };
        auto Lbl = [&](const wchar_t* t, int x, int y, int w, int h) {
            HWND hl = CreateWindowExW(0, L"STATIC", t,
                WS_CHILD | WS_VISIBLE, D(x), D(y), D(w), D(h), hwnd, nullptr, nullptr, nullptr);
            SF(hl, g_ruiFontNorm);
            return hl;
        };

        // Intro
        Lbl(L"This lets you save the last few minutes of your gameplay as a video. Useful to appeal your death in case of a bug or disconnect.", 14, 12, 432, 40);

        // Monitor
        Lbl(L"Monitor:", 14, 56, 80, 16);
        g_ruiComboMonitor = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            D(14), D(74), D(432), D(200), hwnd,
            (HMENU)(UINT_PTR)RID_COMBO_MONITOR, nullptr, nullptr);
        SF(g_ruiComboMonitor, g_ruiFontNorm);
        SetWindowTheme(g_ruiComboMonitor, L"DarkMode_CFD", nullptr);

        // Populate monitors
        auto monitors = RB_EnumMonitors();
        for (auto& m : monitors)
            SendMessageW(g_ruiComboMonitor, CB_ADDSTRING, 0, (LPARAM)m.name.c_str());
        int curMon = RB_GetSettings().monitorIndex;
        if (curMon < 0 || curMon >= (int)monitors.size()) curMon = 0;
        SendMessageW(g_ruiComboMonitor, CB_SETCURSEL, (WPARAM)curMon, 0);

        // Duration
        Lbl(L"Duration:", 14, 120, 70, 16);
        g_ruiEditMinutes = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            std::to_wstring(RB_GetSettings().minutes).c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            D(90), D(118), D(50), D(22), hwnd,
            (HMENU)(UINT_PTR)RID_EDIT_MINUTES, nullptr, nullptr);
        SF(g_ruiEditMinutes, g_ruiFontNorm);
        Lbl(L"minutes  (1-60)", 148, 120, 120, 16);

        // Frame rate
        Lbl(L"Frame rate:", 14, 150, 75, 16);
        g_ruiEditFps = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            std::to_wstring(RB_GetSettings().fps).c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            D(92), D(148), D(50), D(22), hwnd,
            (HMENU)(UINT_PTR)RID_EDIT_FPS, nullptr, nullptr);
        SF(g_ruiEditFps, g_ruiFontNorm);
        Lbl(L"fps  (20-60)   lower = better performance", 148, 150, 290, 16);

        // Save folder
        Lbl(L"Save folder:", 14, 204, 90, 16);
        g_ruiEditFolder = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            RB_GetSettings().saveFolder.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
            D(14), D(222), D(330), D(22), hwnd,
            (HMENU)(UINT_PTR)RID_EDIT_FOLDER, nullptr, nullptr);
        SF(g_ruiEditFolder, g_ruiFontNorm);

        g_ruiBtnFolder = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            D(350), D(222), D(96), D(22), hwnd,
            (HMENU)(UINT_PTR)RID_BTN_FOLDER, nullptr, nullptr);
        SF(g_ruiBtnFolder, g_ruiFontNorm);

        // Prompt-to-save checkbox
        g_ruiCheckPromptSave = CreateWindowExW(0, L"BUTTON",
            L"Prompt to save replay when stopping recording",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            D(14), D(260), D(430), D(18), hwnd,
            (HMENU)(UINT_PTR)RID_CHECK_PROMPT_SAVE, nullptr, nullptr);
        SF(g_ruiCheckPromptSave, g_ruiFontNorm);
        if (RB_GetSettings().promptSaveOnStop)
            SendMessageW(g_ruiCheckPromptSave, BM_SETCHECK, BST_CHECKED, 0);

        // Auto-start checkbox
        g_ruiCheckAutoStart = CreateWindowExW(0, L"BUTTON",
            L"Auto-start recording when hitting Play",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            D(14), D(282), D(430), D(18), hwnd,
            (HMENU)(UINT_PTR)RID_CHECK_AUTOSTART, nullptr, nullptr);
        SF(g_ruiCheckAutoStart, g_ruiFontNorm);
        if (RB_GetSettings().autoStartOnPlay)
            SendMessageW(g_ruiCheckAutoStart, BM_SETCHECK, BST_CHECKED, 0);

        // Start/Stop hotkey
        Lbl(L"Start / Stop hotkey:", 14, 324, 150, 16);
        g_ruiEditHkStart = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_CENTER,
            D(14), D(342), D(200), D(22), hwnd,
            (HMENU)(UINT_PTR)RID_EDIT_HK_START, nullptr, nullptr);
        SF(g_ruiEditHkStart, g_ruiFontNorm);
        g_hkStart = { RB_GetSettings().startStopVK, RB_GetSettings().startStopMods, false };
        UpdateHotkeyEdit(g_ruiEditHkStart, g_hkStart);
        SetWindowSubclass(g_ruiEditHkStart, HkEditSubclass, 20, (DWORD_PTR)&g_hkStart);

        // Save hotkey
        Lbl(L"Save replay hotkey:", 14, 382, 150, 16);
        g_ruiEditHkSave = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_CENTER,
            D(14), D(400), D(200), D(22), hwnd,
            (HMENU)(UINT_PTR)RID_EDIT_HK_SAVE, nullptr, nullptr);
        SF(g_ruiEditHkSave, g_ruiFontNorm);
        g_hkSave = { RB_GetSettings().saveVK, RB_GetSettings().saveMods, false };
        UpdateHotkeyEdit(g_ruiEditHkSave, g_hkSave);
        SetWindowSubclass(g_ruiEditHkSave, HkEditSubclass, 21, (DWORD_PTR)&g_hkSave);

        Lbl(L"(Click then press a Key | Esc = clear)", 220, 346, 230, 16);
        Lbl(L"(Click then press a Key | Esc = clear)", 220, 404, 230, 16);

        // Start/Stop & Save buttons
        g_ruiBtnStartStop = CreateWindowExW(0, L"BUTTON",
            RB_IsRunning() ? L"Stop Recording" : L"Start Recording",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            D(14), D(462), D(160), D(36), hwnd,
            (HMENU)(UINT_PTR)RID_BTN_STARTSTOP, nullptr, nullptr);
        SF(g_ruiBtnStartStop, g_ruiFontBold);

        g_ruiBtnSave = CreateWindowExW(0, L"BUTTON", L"Save Replay Now",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | WS_DISABLED,
            D(184), D(462), D(160), D(36), hwnd,
            (HMENU)(UINT_PTR)RID_BTN_SAVENOW, nullptr, nullptr);
        SF(g_ruiBtnSave, g_ruiFontBold);

        g_ruiBtnClose = CreateWindowExW(0, L"BUTTON", L"Save && Close",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            D(384), D(462), D(160), D(36), hwnd,
            (HMENU)(UINT_PTR)RID_BTN_CLOSE, nullptr, nullptr);
        SF(g_ruiBtnClose, g_ruiFontBold);

        g_ruiStatus = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            D(14), D(522), D(432), D(18), hwnd,
            (HMENU)(UINT_PTR)RID_STATIC_STATUS, nullptr, nullptr);
        SF(g_ruiStatus, g_ruiFontBold);

        SetWindowSubclass(g_ruiBtnStartStop, BtnSubRui, 30, (DWORD_PTR)&g_ruiBtnSSHover);
        SetWindowSubclass(g_ruiBtnSave,      BtnSubRui, 31, (DWORD_PTR)&g_ruiBtnSvHover);
        SetWindowSubclass(g_ruiBtnClose,     BtnSubRui, 33, (DWORD_PTR)&g_ruiBtnCloseHover);
        SetWindowSubclass(g_ruiBtnFolder,    BtnSubRui, 32, (DWORD_PTR)&g_ruiBtnFolderHover);

        SetTimer(hwnd, RID_TIMER_POLL, 400, nullptr);
        RefreshStatus();
        break;
    }

    case WM_TIMER:
        if (wp == RID_TIMER_HOVER) {
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
            step(g_ruiBtnSSHover,     g_ruiBtnSSHoverT,     g_ruiBtnStartStop);
            step(g_ruiBtnSvHover,     g_ruiBtnSvHoverT,     g_ruiBtnSave);
            step(g_ruiBtnCloseHover,  g_ruiBtnCloseHoverT,  g_ruiBtnClose);
            step(g_ruiBtnFolderHover, g_ruiBtnFolderHoverT, g_ruiBtnFolder);
            if (!anyActive) KillTimer(hwnd, RID_TIMER_HOVER);
            return 0;
        }
        if (wp == RID_TIMER_POLL) RefreshStatus();
        return 0;

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);

        if (id == RID_CHECK_AUTOSTART && code == BN_CLICKED) {
            ReplaySettings s = RB_GetSettings();
            s.autoStartOnPlay = (SendMessageW(g_ruiCheckAutoStart, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RB_SetSettings(s);
            break;
        }

        if (id == RID_BTN_FOLDER) {
            IFileOpenDialog* pDlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg)))) {
                DWORD opts = 0; pDlg->GetOptions(&opts);
                pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                pDlg->SetTitle(L"Choose folder to save replays in");
                std::wstring cur = GetEditText(g_ruiEditFolder);
                if (!cur.empty()) {
                    IShellItem* pInit = nullptr;
                    if (SUCCEEDED(SHCreateItemFromParsingName(cur.c_str(), nullptr, IID_PPV_ARGS(&pInit)))) {
                        pDlg->SetFolder(pInit); pInit->Release();
                    }
                }
                if (SUCCEEDED(pDlg->Show(hwnd))) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                        wchar_t* path = nullptr;
                        pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
                        if (path) { SetWindowTextW(g_ruiEditFolder, path); CoTaskMemFree(path); }
                        pItem->Release();
                    }
                }
                pDlg->Release();
            }
            break;
        }

        if (id == RID_BTN_STARTSTOP) {
            ReplaySettings s = CollectSettings();
            RB_SetSettings(s);
            RB_UnregisterHotkeys();
            RegisterHotkeysWithFeedback(hwnd);
            SendMessageW(g_ruiOwner, WM_APP + 30, 0, 0);

            if (RB_IsRunning()) {
                if (RB_GetSettings().promptSaveOnStop) {
                    int r = MessageBoxW(hwnd,
                        L"Save the replay before stopping?",
                        L"Save Replay",
                        MB_YESNOCANCEL | MB_ICONQUESTION);
                    if (r == IDCANCEL) break;
                    if (r == IDYES) { RB_SaveNow(); Sleep(100); }
                }
                RB_Stop();
            } else {
                RB_Start();
            }

            RefreshStatus();
            break;
        }

        if (id == RID_BTN_SAVENOW) {
            RB_SaveNow();
            break;
        }
        if (id == RID_BTN_CLOSE) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        auto ss = [](float r) { return r * r * (3.0f - 2.0f * r); };
        if (dis->hwndItem == g_ruiBtnStartStop)
            DrawDarkButton(dis, ss(g_ruiBtnSSHoverT), true);
        else if (dis->hwndItem == g_ruiBtnSave)
            DrawDarkButton(dis, ss(g_ruiBtnSvHoverT), false);
        else if (dis->hwndItem == g_ruiBtnClose)
            DrawDarkButton(dis, ss(g_ruiBtnCloseHoverT), false, true);
        else if (dis->hwndItem == g_ruiBtnFolder)
            DrawDarkButton(dis, ss(g_ruiBtnFolderHoverT), false);
        else break;
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc  = (HDC)wp;
        HWND ctl = (HWND)lp;
        SetBkColor(hdc, RUI_BG);
        if (ctl == g_ruiStatus) {
            SetTextColor(hdc, RB_IsRunning() ? RUI_GREEN : RUI_DIM);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        SetTextColor(hdc, RUI_TEXT);
        return (LRESULT)g_ruiBrBg;
    }

    case WM_CTLCOLOREDIT: {
        SetBkColor((HDC)wp, RUI_BG2);
        SetTextColor((HDC)wp, RUI_TEXT);
        return (LRESULT)g_ruiBrBg2;
    }

    case WM_CTLCOLORBTN: {
        SetBkColor((HDC)wp, RUI_BG);
        return (LRESULT)g_ruiBrBg;
    }

    case WM_CTLCOLORLISTBOX: {
        SetBkColor((HDC)wp, RUI_BG2);
        SetTextColor((HDC)wp, RUI_TEXT);
        return (LRESULT)g_ruiBrBg2;
    }

    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_ruiBrBg);
        return 1;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        UINT dpi = GetDpiForWindow(hwnd);
        LONG w = MulDiv(574, dpi, 96);
        LONG h = MulDiv(552, dpi, 96);
        mmi->ptMinTrackSize = { w, h };
        mmi->ptMaxTrackSize = { w, h };
        break;
    }

    case WM_DPICHANGED: {
        UINT oldDpi = g_ruiCurrentDpi;
        UINT newDpi = LOWORD(wp);
        g_ruiCurrentDpi = newDpi;

        // Resize window to the rect Windows suggests
        RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // Rescale every child control position/size proportionally
        struct RD { UINT o, n; };
        RD rd = { oldDpi, newDpi };
        EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
            auto* d = reinterpret_cast<RD*>(lp);
            RECT cr; GetWindowRect(child, &cr);
            POINT pt = { cr.left, cr.top };
            ScreenToClient(GetParent(child), &pt);
            int x = MulDiv(pt.x,             d->n, d->o);
            int y = MulDiv(pt.y,             d->n, d->o);
            int w = MulDiv(cr.right - cr.left, d->n, d->o);
            int h = MulDiv(cr.bottom - cr.top, d->n, d->o);
            SetWindowPos(child, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&rd));

        // Recreate fonts at new DPI
        auto MkF = [newDpi](int pt, int wt) -> HFONT {
            LOGFONTW lf = {}; lf.lfHeight = -MulDiv(pt, newDpi, 72); lf.lfWeight = wt;
            lf.lfCharSet = DEFAULT_CHARSET; lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Segoe UI"); return CreateFontIndirectW(&lf);
        };
        HFONT newNorm  = MkF(9, FW_NORMAL);
        HFONT newBold  = MkF(9, FW_SEMIBOLD);
        HFONT newSmall = MkF(8, FW_NORMAL);

        // Reassign fonts by matching each control's current font to the right new one
        struct FD { HFONT oN, oB, oS, nN, nB, nS; };
        FD fd = { g_ruiFontNorm, g_ruiFontBold, g_ruiFontSmall, newNorm, newBold, newSmall };
        EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
            auto* d = reinterpret_cast<FD*>(lp);
            HFONT cur = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
            HFONT rep = d->nN;
            if (cur == d->oB) rep = d->nB;
            else if (cur == d->oS) rep = d->nS;
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(rep), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&fd));

        DeleteObject(g_ruiFontNorm);  g_ruiFontNorm  = newNorm;
        DeleteObject(g_ruiFontBold);  g_ruiFontBold  = newBold;
        DeleteObject(g_ruiFontSmall); g_ruiFontSmall = newSmall;

        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_CLOSE:
        {
            ReplaySettings s = CollectSettings();
            RB_SetSettings(s);
            int hkFail = RB_RegisterHotkeys();
            SendMessageW(g_ruiOwner, WM_APP + 30, 0, 0); // save (with cleared VKs if conflict)
            if (hkFail & RB_HK_STARTSTOP_FAILED) {
                g_hkStart = { 0, 0, false };
                UpdateHotkeyEdit(g_ruiEditHkStart, g_hkStart);
                MessageBoxW(hwnd,
                    L"The Start/Stop hotkey is already in use by another application.\n"
                    L"The hotkey has been cleared. Please choose a different key.",
                    L"Hotkey Conflict", MB_OK | MB_ICONWARNING);
                SetFocus(hwnd);
            }
            if (hkFail & RB_HK_SAVE_FAILED) {
                g_hkSave = { 0, 0, false };
                UpdateHotkeyEdit(g_ruiEditHkSave, g_hkSave);
                MessageBoxW(hwnd,
                    L"The Save Replay hotkey is already in use by another application.\n"
                    L"The hotkey has been cleared. Please choose a different key.",
                    L"Hotkey Conflict", MB_OK | MB_ICONWARNING);
                SetFocus(hwnd);
            }
            if (hkFail) return 0; // keep window open so user can pick a different key
        }
        KillTimer(hwnd, RID_TIMER_POLL);
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, RID_TIMER_POLL);
        if (g_ruiFontNorm)  { DeleteObject(g_ruiFontNorm);  g_ruiFontNorm  = nullptr; }
        if (g_ruiFontBold)  { DeleteObject(g_ruiFontBold);  g_ruiFontBold  = nullptr; }
        if (g_ruiFontSmall) { DeleteObject(g_ruiFontSmall); g_ruiFontSmall = nullptr; }
        if (g_ruiBrBg)      { DeleteObject(g_ruiBrBg);      g_ruiBrBg      = nullptr; }
        if (g_ruiBrBg2)     { DeleteObject(g_ruiBrBg2);     g_ruiBrBg2     = nullptr; }
        g_ruiHwnd  = nullptr;
        g_ruiOwner = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Public API ────────────────────────────────────────────────────────────────
void ShowReplayWindow(HWND hParent)
{
    if (g_ruiHwnd) {
        SetForegroundWindow(g_ruiHwnd);
        ShowWindow(g_ruiHwnd, SW_SHOW);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ReplayWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"WOWHCReplaySettings";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    UINT dpi = GetDpiForWindow(hParent ? hParent : GetDesktopWindow());
    int w = MulDiv(574, dpi, 96);
    int h = MulDiv(552, dpi, 96);

    RECT pr; GetWindowRect(hParent, &pr);
    int offset = MulDiv(100, dpi, 96);
    int x = pr.left + (pr.right - pr.left - w) / 2 + offset;
    int y = pr.top  + (pr.bottom - pr.top - h) / 2;

    HWND hwnd = CreateWindowExW(0,
        L"WOWHCReplaySettings", L"Screen Recorder Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h,
        hParent, nullptr, GetModuleHandleW(nullptr), nullptr);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    // Inherit icon from main launcher window
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

void CloseReplayWindow()
{
    if (g_ruiHwnd) SendMessageW(g_ruiHwnd, WM_CLOSE, 0, 0);
}

HWND GetReplayWindowHwnd()
{
    return g_ruiHwnd;
}
