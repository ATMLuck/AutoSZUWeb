#include "sys.h"
#include <iostream>
#include <windows.h>
void SetAutoStart()
{
    WCHAR AppPath[MAX_PATH];
    GetModuleFileNameW(NULL, AppPath, MAX_PATH);
    std::wstring appName = L"AutoSZUWeb";
    std::wstring appPath = AppPath;

    HKEY hKey;
    LPCWSTR subKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        subKey,
        0,
        KEY_SET_VALUE,
        &hKey
    );
    if (result != ERROR_SUCCESS)
    {
        MessageBoxW(NULL,L"打开注册表失败",L"提示",MB_OK);
    }
    result = RegSetValueExW(
        hKey,
        appName.c_str(),
        0,
        REG_SZ,
        (const BYTE*)appPath.c_str(),
        (appPath.size() + 1) * sizeof(WCHAR)
    );
    RegCloseKey(hKey);
    if (result != ERROR_SUCCESS)
    {
        MessageBoxW(NULL,L"写入注册表失败",L"提示",MB_OK);
    }
}