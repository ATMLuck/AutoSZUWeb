#pragma once

#include <filesystem>

namespace fs = std::filesystem;

// Windows: %APPDATA%; macOS: ~/Library/Application Support。
fs::path GetUsersFolderPath();

// Windows/macOS 的用户桌面目录。
fs::path GetDesktopPath();

// 应用配置文件：<数据根目录>/AutoSZUWeb/setting.json。
fs::path GetConfigPath();
