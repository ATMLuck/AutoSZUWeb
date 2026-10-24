#pragma once

#include <filesystem>
#include <string>

namespace Config
{
    // 读取 setting.json 的 AutoAuth 字段（是否后台自动认证）。
    // 文件缺失、字段缺失或解析失败时返回 true（默认开启，保持旧行为）。
    bool ReadAutoAuthEnabled(const std::filesystem::path& configPath);

    // 设置 setting.json 的 AutoAuth 字段并原子写回，保留其他字段。
    // 配置文件不存在或格式错误时返回 false 并通过 error 说明。
    bool WriteAutoAuthEnabled(const std::filesystem::path& configPath,
                              bool enabled, std::string& error);
}
