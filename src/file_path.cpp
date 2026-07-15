#include "file_path.h"
#include <vector>
#include <filesystem>
#include <windows.h>
#include <shlobj.h>
fs::path GetUsersFolderPath()
{
    WCHAR widePath[MAX_PATH];
    HRESULT result = SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, widePath);
    if (FAILED(result))
    {
        return "";
    }
    int bufferSize = WideCharToMultiByte
    (
        CP_ACP,
        0,
        widePath,
        -1,
        NULL,
        0,
        NULL, NULL
    );
    if (bufferSize <= 0)
    {
        return "";
    }
    std::vector<char> multiByteBuffer(bufferSize);
    WideCharToMultiByte
    (
        CP_ACP,
        0,
        widePath,
        -1,
        multiByteBuffer.data(),
        bufferSize,
        NULL, NULL
    );
    return fs::path(multiByteBuffer.data());
}
fs::path GetDesktopPath()
{
    WCHAR widePath[MAX_PATH];
    HRESULT result = SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, widePath);
    if (FAILED(result))
    {
        return "";
    }

    int bufferSize = WideCharToMultiByte
    (
        CP_ACP,
        0,
        widePath,
        -1,
        NULL,
        0,
        NULL, NULL
    );
    if (bufferSize <= 0)
    {
        return "";
    }
    std::vector<char> multiByteBuffer(bufferSize);
    WideCharToMultiByte
    (
        CP_ACP,
        0,
        widePath,
        -1,
        multiByteBuffer.data(),
        bufferSize,
        NULL, NULL
    );
    return fs::path(multiByteBuffer.data());
}