#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace Cli
{
    enum class Action
    {
        NormalStartup, // 无参数：后台常驻模式
        OneShotAuth,   // 卡号 密码：手动认证一次后退出
        SetAutoAuth,   // --auto true|false：修改后台自动认证开关
        Uninstall,     // --uninstall：从本机完全卸载
        Help,          // --help / -h
        UsageError     // 未知或多余参数
    };

    struct Parsed
    {
        Action action = Action::NormalStartup;
        std::string account;
        std::string password;
        bool autoAuthEnabled = false;
    };

    // 解析不含程序名的命令行参数（UTF-8）。纯函数，无副作用。
    Parsed Parse(const std::vector<std::string>& args);

    void PrintUsage(std::ostream& output);
}
