#include "file_path.h"

#include <cstdlib>
#include <system_error>

#include <windows.h>
#include <shlobj.h>
#include <vector>

namespace
{
    fs::path EnvironmentPath(const char* name)
    {
        const char* value = std::getenv(name);
        return (value && *value) ? fs::u8path(value) : fs::path{};
    }

    fs::path KnownFolderPath(int folderId)
    {
        WCHAR widePath[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathW(nullptr, folderId, nullptr, 0, widePath)))
            return {};
        return fs::path(widePath);
    }
}

fs::path GetUsersFolderPath()
{
    fs::path overridePath = EnvironmentPath("AUTOSZUWEB_DATA_ROOT");
    if (!overridePath.empty())
        return overridePath;

    return KnownFolderPath(CSIDL_APPDATA);
}

fs::path GetDesktopPath()
{
    fs::path overridePath = EnvironmentPath("AUTOSZUWEB_DESKTOP_ROOT");
    if (!overridePath.empty())
        return overridePath;

    return KnownFolderPath(CSIDL_DESKTOP);
}

fs::path GetConfigPath()
{
    fs::path dataRoot = GetUsersFolderPath();
    if (dataRoot.empty())
        return {};

    fs::path dir = dataRoot / "AutoSZUWeb";
    std::error_code ec;
    fs::create_directories(dir, ec);

    fs::path newPath = dir / "setting.json";

    fs::path oldPath = dataRoot / "autoWEB.json";
    if (fs::exists(oldPath, ec) && fs::is_regular_file(oldPath, ec))
    {
        bool migrated = fs::exists(newPath, ec);
        if (!migrated)
        {
            ec.clear();
            migrated = fs::copy_file(oldPath, newPath,
                fs::copy_options::overwrite_existing, ec) && !ec;
        }
        if (migrated)
            fs::remove(oldPath, ec);
    }

    return newPath;
}
