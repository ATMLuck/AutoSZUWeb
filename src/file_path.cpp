#include "file_path.h"

#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <vector>
#endif

namespace
{
    fs::path EnvironmentPath(const char* name)
    {
        const char* value = std::getenv(name);
        return (value && *value) ? fs::u8path(value) : fs::path{};
    }

#ifdef _WIN32
    fs::path KnownFolderPath(int folderId)
    {
        WCHAR widePath[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathW(nullptr, folderId, nullptr, 0, widePath)))
            return {};
        return fs::path(widePath);
    }
#endif
}

fs::path GetUsersFolderPath()
{
    fs::path overridePath = EnvironmentPath("AUTOSZUWEB_DATA_ROOT");
    if (!overridePath.empty())
        return overridePath;

#ifdef _WIN32
    return KnownFolderPath(CSIDL_APPDATA);
#elif defined(__APPLE__)
    fs::path home = EnvironmentPath("HOME");
    return home.empty() ? fs::path{} : home / "Library" / "Application Support";
#elif defined(__linux__) && defined(AUTOSZUWEB_LINUX_TEST)
    fs::path home = EnvironmentPath("HOME");
    return home.empty() ? fs::path{} : home / ".local" / "share";
#else
#error "Unsupported platform"
#endif
}

fs::path GetDesktopPath()
{
    fs::path overridePath = EnvironmentPath("AUTOSZUWEB_DESKTOP_ROOT");
    if (!overridePath.empty())
        return overridePath;

#ifdef _WIN32
    return KnownFolderPath(CSIDL_DESKTOP);
#elif defined(__APPLE__) || (defined(__linux__) && defined(AUTOSZUWEB_LINUX_TEST))
    fs::path home = EnvironmentPath("HOME");
    return home.empty() ? fs::path{} : home / "Desktop";
#else
#error "Unsupported platform"
#endif
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

#ifdef _WIN32
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
#endif

    return newPath;
}
