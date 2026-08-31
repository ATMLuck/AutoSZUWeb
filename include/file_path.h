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
/*
    配置文件路径 "~/AppData/Roaming/AutoSZUWeb/setting.json"，
    自动建目录；检测到旧配置 autoWEB.json 时迁移并删除旧文件
*/
fs::path GetConfigPath();