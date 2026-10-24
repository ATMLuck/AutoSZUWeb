#include "file_path.h"

#include <cstdlib>
#include <system_error>

namespace
{
    fs::path EnvironmentPath(const char* name)
    {
        const char* value = std::getenv(name);
        return (value && *value) ? fs::u8path(value) : fs::path{};
    }
}

fs::path GetUsersFolderPath()
{
    fs::path overridePath = EnvironmentPath("AUTOSZUWEB_DATA_ROOT");
    if (!overridePath.empty())
        return overridePath;

    fs::path home = EnvironmentPath("HOME");
    return home.empty() ? fs::path{} : home / "Library" / "Application Support";
}

fs::path GetDesktopPath()
{
    fs::path overridePath = EnvironmentPath("AUTOSZUWEB_DESKTOP_ROOT");
    if (!overridePath.empty())
        return overridePath;

    fs::path home = EnvironmentPath("HOME");
    return home.empty() ? fs::path{} : home / "Desktop";
}

fs::path GetConfigPath()
{
    fs::path dataRoot = GetUsersFolderPath();
    if (dataRoot.empty())
        return {};

    fs::path dir = dataRoot / "AutoSZUWeb";
    std::error_code ec;
    fs::create_directories(dir, ec);

    return dir / "setting.json";
}
