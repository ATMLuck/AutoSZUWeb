#pragma once
#include <filesystem>
namespace fs = std::filesystem;
/*
    ruturn "~/appdata/Roaming" path on Windows
*/
fs::path GetUsersFolderPath();
/*
    ruturn "~/Desktop" path on Windows
*/
fs::path GetDesktopPath();