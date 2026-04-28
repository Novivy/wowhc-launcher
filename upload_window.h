#pragma once
#include <windows.h>
#include <string>

void ShowUploadWindow(HWND hParent, const std::wstring& configDir);
void CloseUploadWindow();
