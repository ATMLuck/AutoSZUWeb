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
fs::path GetConfigPath()
{
    fs::path appdata = GetUsersFolderPath();
    fs::path dir = appdata / "AutoSZUWeb";
    std::error_code ec;
    fs::create_directories(dir, ec); // 已存在则忽略

    fs::path newPath = dir / "setting.json";
    fs::path oldPath = appdata / "autoWEB.json";

    // 迁移兼容: 旧配置存在时, 复制到新位置(若新位置尚无), 确认新文件就位后删除旧文件
    if (fs::exists(oldPath, ec) && fs::is_regular_file(oldPath, ec))
    {
        bool migrated = fs::exists(newPath, ec);
        if (!migrated)
            migrated = fs::copy_file(oldPath, newPath, fs::copy_options::overwrite_existing, ec) && !ec;
        if (migrated)
            fs::remove(oldPath, ec);
    }
    return newPath;
}
