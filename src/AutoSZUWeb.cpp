#include "app_ui.h"
#include "cli.h"
#include "file_path.h"
#include "runtime.h"
#include "single_instance.h"
#include "sys.h"
#include "web.h"
#include "user_data.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

namespace
{
    bool FirstBoot = true;

    [[noreturn]] void ExitWithMessage(const std::string& message)
    {
        AppUI::ShowMessage(message, "提示");
        std::exit(1);
    }

    // 手动认证模式：认证一次后退出，不写配置、不注册自启动、不进入监控循环。
    int RunOneShotAuth(const std::string& account, const std::string& password)
    {
        FirstBoot = false; // 抑制首启认证弹窗，改用控制台输出。
        std::string message;
        const bool ok = Login(account, password, message);
        if (PrepareCliConsole())
            std::cout << (ok ? "认证成功: " : "认证失败: ") << message << '\n';
        else
            AppUI::ShowMessage(message, ok ? "认证成功" : "认证失败");
        return ok ? 0 : 1;
    }

    void RunMainLoop()
    {
        const fs::path loginPath = GetConfigPath();
        if (loginPath.empty())
            ExitWithMessage("无法确定配置文件路径。");

        nlohmann::json loginData;
        try
        {
            std::ifstream loginFile(loginPath);
            if (!loginFile)
                ExitWithMessage("无法读取配置文件：" + loginPath.u8string());
            loginFile >> loginData;
        }
        catch (const std::exception& error)
        {
            ExitWithMessage("配置文件格式错误，请删除后重新配置。\n" +
                std::string(error.what()));
        }

        if (!loginData.contains("Account") || !loginData["Account"].is_string() ||
            !loginData.contains("Password") || !loginData["Password"].is_string())
            ExitWithMessage("配置文件缺少账号或密码，请删除后重新配置。");

        const std::string account = DecryptStr(loginData["Account"].get<std::string>());
        const std::string password = DecryptStr(loginData["Password"].get<std::string>());
        if (account.empty() || password.empty())
            ExitWithMessage("无法解密账号或密码，请删除配置文件后重新配置。");

        // 每次启动都修复/刷新自启动配置，允许用户移动 .app 后自动更新路径。
        SetAutoStart();

        const bool online = Login(account, password);
        FirstBoot = false;

        Runtime::MonitorCallbacks callbacks;
        callbacks.login = [&] { return Login(account, password); };
        callbacks.networkCheck = [] { return NetworkCheck(); };
        callbacks.sleep = [](std::chrono::milliseconds duration) {
            std::this_thread::sleep_for(duration);
        };
        callbacks.shouldStop = [] { return false; };
        Runtime::RunMonitorLoop(
            online ? Runtime::ConnectionState::Online
                   : Runtime::ConnectionState::Offline,
            callbacks);
    }

    void ConfigureNewUser()
    {
        const fs::path desktop = GetDesktopPath();
        if (desktop.empty())
            ExitWithMessage("无法确定桌面路径。");
        const fs::path inputPath = desktop / "userdata.txt";

        if (!fs::exists(inputPath) || !fs::is_regular_file(inputPath))
        {
            if (AppUI::ShowMessage(
                    "请打开桌面上的 userdata.txt 文件，并按照文件内提示写入校园卡号和统一身份认证平台密码，然后重新启动本程序。",
                    "提示", true) == AppUI::Button::Cancel)
                std::exit(0);

            std::error_code error;
            fs::create_directories(desktop, error);
            std::string fileError;
            if (!UserData::CreateTemplate(inputPath, fileError))
                ExitWithMessage(fileError + "。macOS 首次使用时请允许访问桌面文件夹。");
            return;
        }

        std::string account;
        std::string password;
        std::string fileError;
        if (!UserData::ReadCredentials(inputPath, account, password, fileError))
            ExitWithMessage(fileError + "。");

        const std::string encryptedAccount = EncryptStr(account);
        const std::string encryptedPassword = EncryptStr(password);
        if (encryptedAccount.empty() || encryptedPassword.empty())
        {
            const std::string detail = GetCredentialError();
            ExitWithMessage("系统凭据保护失败，未保存账号密码。"
                + (detail.empty() ? std::string{} : "\n" + detail));
        }

        const fs::path configPath = GetConfigPath();
        if (configPath.empty())
            ExitWithMessage("无法确定配置文件路径。");
        fs::path temporaryPath = configPath;
        temporaryPath += ".tmp";
        {
            std::ofstream output(temporaryPath, std::ios::trunc);
            if (!output)
                ExitWithMessage("无法创建配置文件。");
            output << nlohmann::json{
                {"Account", encryptedAccount},
                {"Password", encryptedPassword}
            }.dump(2);
            if (!output)
                ExitWithMessage("写入配置文件失败。");
        }

        std::error_code error;
        fs::rename(temporaryPath, configPath, error);
        if (error)
        {
            fs::remove(configPath, error);
            error.clear();
            fs::rename(temporaryPath, configPath, error);
        }
        if (error)
            ExitWithMessage("保存配置文件失败：" + error.message());
        std::string removeError;
        if (!UserData::RemoveFile(inputPath, removeError))
            AppUI::ShowMessage(removeError +
                "。请关闭正在占用该文件的程序后手动删除。", "提示");
        RunMainLoop();
    }
}

// 供 web.cpp 控制首次认证弹窗。
bool IsFirstBoot()
{
    return FirstBoot;
}

int main(int argc, char* argv[])
{
    const Cli::Parsed parsed = Cli::Parse(GetUtf8Arguments(argc, argv));

    if (parsed.action == Cli::Action::Help)
    {
        if (PrepareCliConsole())
            Cli::PrintUsage(std::cout);
        return 0;
    }
    if (parsed.action == Cli::Action::UsageError)
    {
        if (PrepareCliConsole())
        {
            std::cerr << "参数无效。\n\n";
            Cli::PrintUsage(std::cerr);
        }
        return 2;
    }
    if (parsed.action == Cli::Action::OneShotAuth)
        return RunOneShotAuth(parsed.account, parsed.password);

    const fs::path configPath = GetConfigPath();
    if (configPath.empty())
        ExitWithMessage("无法确定配置文件路径。");


    Runtime::SingleInstanceGuard instanceGuard(
        configPath.parent_path() / "AutoSZUWeb.instance.lock");
    if (!instanceGuard.IsPrimary())
        return 0;

    if (fs::exists(configPath) && fs::is_regular_file(configPath))
        RunMainLoop();
    else
        ConfigureNewUser();
    return 0;
}
