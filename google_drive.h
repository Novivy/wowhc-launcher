#pragma once
#include <windows.h>
#include <string>
#include <functional>

enum class GDriveError { None, AuthFailed, NetworkError, ApiError, Cancelled };

struct GDriveResult {
    GDriveError  error = GDriveError::None;
    std::wstring shareLink;
    std::wstring errorMsg;
};

using GDriveProgressCb = std::function<void(DWORD64 done, DWORD64 total)>;
using GDriveStatusCb   = std::function<void(const std::wstring& msg)>;

bool         GDrive_HasToken(const std::wstring& configDir);
bool         GDrive_HasClientId();
bool         GDrive_HasFolder();
std::wstring GDrive_GetFolderUrl();
void         GDrive_Disconnect(const std::wstring& configDir);
GDriveResult GDrive_Upload(const std::wstring& filePath,
                            const std::wstring& configDir,
                            GDriveProgressCb    onProgress,
                            GDriveStatusCb      onStatus);
