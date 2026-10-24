#include "cli.h"

#include <ostream>

namespace Cli
{
    namespace
    {
        bool IsOption(const std::string& argument)
        {
            return argument.size() >= 2 && argument[0] == '-';
        }

        bool EqualsIgnoreCase(const std::string& text, const char* literal)
        {
            if (text.size() != std::string(literal).size())
                return false;
            for (size_t i = 0; i < text.size(); ++i)
            {
                char c = text[i];
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
                if (c != literal[i])
                    return false;
            }
            return true;
        }
    }

    Parsed Parse(const std::vector<std::string>& args)
    {
        Parsed parsed;
        if (args.empty())
        {
            parsed.action = Action::NormalStartup;
            return parsed;
        }

        if (args.size() == 1)
        {
            if (args[0] == "--uninstall")
                parsed.action = Action::Uninstall;
            else if (args[0] == "--help" || args[0] == "-h")
                parsed.action = Action::Help;
            else
                parsed.action = Action::UsageError;
            return parsed;
        }

        if (args.size() == 2 && args[0] == "--auto")
        {
            if (EqualsIgnoreCase(args[1], "true"))
            {
                parsed.action = Action::SetAutoAuth;
                parsed.autoAuthEnabled = true;
            }
            else if (EqualsIgnoreCase(args[1], "false"))
            {
                parsed.action = Action::SetAutoAuth;
                parsed.autoAuthEnabled = false;
            }
            else
                parsed.action = Action::UsageError;
            return parsed;
        }

        if (args.size() == 2 && !IsOption(args[0]) && !IsOption(args[1]))
        {
            parsed.action = Action::OneShotAuth;
            parsed.account = args[0];
            parsed.password = args[1];
            return parsed;
        }

        parsed.action = Action::UsageError;
        return parsed;
    }

    void PrintUsage(std::ostream& output)
    {
        output
            << "用法: AutoSZUWeb [选项] [卡号 密码]\n"
            << "\n"
            << "  (无参数)        后台常驻模式：首次配置并自动认证、监控网络状态\n"
            << "  卡号 密码       手动认证一次后退出（成功退出码 0，失败 1）\n"
            << "  --auto true|false\n"
            << "                  开启/关闭后台自动认证（写入 setting.json，\n"
            << "                  运行中的实例约 10 秒内生效；手动认证不受影响）\n"
            << "  --uninstall     从本机完全卸载（自启动、配置、日志、密钥与程序本体）\n"
            << "  --help, -h      显示本帮助\n";
    }
}
