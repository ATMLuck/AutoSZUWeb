#pragma once

#include <filesystem>

namespace fs = std::filesystem;

// Windows: %APPDATA%; macOS: ~/Library/Application Support。
// 测试时可通过 AUTOSZUWEB_DATA_ROOT 覆盖。
fs::path GetUsersFolderPath();

// Windows/macOS 的用户桌面目录。
// 测试时可通过 AUTOSZUWEB_DESKTOP_ROOT 覆盖。
fs::path GetDesktopPath();

// 应用配置文件：<数据根目录>/AutoSZUWeb/setting.json。
// 自动创建目录；Windows 会迁移旧的 %APPDATA%/autoWEB.json。
fs::path GetConfigPath();
