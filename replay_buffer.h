#pragma once
#include <windows.h>
#include <string>
#include <vector>

#define HOTKEY_ID_RB_STARTSTOP  1001
#define HOTKEY_ID_RB_SAVE       1002
#define WM_RB_STATUS            (WM_APP + 20)  // wParam=1 started, 0 stopped

struct ReplaySettings {
    int  minutes      = 2;
    int  fps          = 30;  // 20-60
    int  ramMB        = 0;   // 0 = auto (15% total RAM)
    int  monitorIndex = 0;
    std::wstring saveFolder;
    UINT startStopVK      = 0;
    UINT startStopMods    = 0;
    UINT saveVK           = 0;
    UINT saveMods         = 0;
    bool promptSaveOnStop  = true;
    bool autoStartOnPlay   = false;
};

struct MonitorDesc {
    HMONITOR hmon;
    std::wstring name;
    int adapterIdx;
    int outputIdx;
};

void                     RB_Init(HWND hMainWnd);
void                     RB_Shutdown();
std::vector<MonitorDesc> RB_EnumMonitors();
bool                     RB_Start();
void                     RB_Stop();
void                     RB_SaveNow();
bool                     RB_IsRunning();
void                     RB_SetSettings(const ReplaySettings& s);   // set only, no restart
void                     RB_ApplySettings(const ReplaySettings& s); // set + restart if running
const ReplaySettings&    RB_GetSettings();
enum OsdAccent { OSD_GREEN = 0, OSD_RED = 1, OSD_ORANGE = 2 };
void                     RB_ShowOsd(const std::wstring& text, OsdAccent accent = OSD_GREEN);
// Bitmask returned by RB_RegisterHotkeys
#define RB_HK_STARTSTOP_FAILED 0x1
#define RB_HK_SAVE_FAILED      0x2
int                      RB_RegisterHotkeys();  // returns 0 on success, bitmask of failures
void                     RB_UnregisterHotkeys();
void                     RB_SetDllDir(const std::wstring& dir); // set directory to load FFmpeg DLLs from
void                     RB_SetLogPath(const std::wstring& path); // set log file path (same as main launcher.log)
void                     SaveReplaySettings(const ReplaySettings& s, const std::wstring& iniPath);
ReplaySettings           LoadReplaySettings(const std::wstring& iniPath);
std::wstring             FormatHotkey(UINT vk, UINT mods);
