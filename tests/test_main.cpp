#include "app_ui.h"
#include "cli.h"
#include "config.h"
#include "file_path.h"
#include "network.h"
#include "platform_support.h"
#include "runtime.h"
#include "single_instance.h"
#include "sys.h"
#include "uninstall.h"
#include "user_data.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef _WIN32
using TestSocket = SOCKET;
constexpr TestSocket kInvalidTestSocket = INVALID_SOCKET;
#else
using TestSocket = int;
constexpr TestSocket kInvalidTestSocket = -1;
#endif

namespace fs = std::filesystem;

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& name)
    {
        if (condition)
            std::cout << "[PASS] " << name << '\n';
        else
        {
            std::cerr << "[FAIL] " << name << '\n';
            ++failures;
        }
    }

    void SetEnvironment(const char* name, const std::string& value)
    {
#ifdef _WIN32
        _putenv_s(name, value.c_str());
#else
        setenv(name, value.c_str(), 1);
#endif
    }

    std::string ReadAll(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
    }

    void TestEncryption()
    {
        const std::string plaintext = "2026123456|测试密码!@#";
        const std::string encrypted = EncryptStr(plaintext);
#ifdef _WIN32
        const std::string credentialError = GetCredentialError();
        if (encrypted.empty() &&
            (credentialError.find("错误码=2") != std::string::npos ||
             credentialError.find("错误码=1312") != std::string::npos))
        {
            std::cout << "[SKIP] DPAPI is unavailable in this sandboxed/non-interactive session: "
                      << credentialError << '\n';
            return;
        }
#endif
        if (encrypted.empty() && !GetCredentialError().empty())
            std::cerr << "[DETAIL] " << GetCredentialError() << '\n';
        Check(!encrypted.empty(), "credential encryption returns data");
        Check(encrypted != plaintext, "credential is not stored as plaintext");
        Check(DecryptStr(encrypted) == plaintext, "credential encryption round trip");

        std::string tampered = encrypted;
        if (!tampered.empty())
            tampered[tampered.size() / 2] = tampered[tampered.size() / 2] == 'A' ? 'B' : 'A';
        Check(DecryptStr(tampered).empty(), "tampered credential is rejected");
    }

    void TestCli(int argc, char* argv[])
    {
        Check(Cli::Parse({}).action == Cli::Action::NormalStartup,
            "cli: no arguments starts the resident mode");
        Check(Cli::Parse({"--uninstall"}).action == Cli::Action::Uninstall,
            "cli: --uninstall selects uninstall mode");
        Check(Cli::Parse({"--help"}).action == Cli::Action::Help,
            "cli: --help selects help");
        Check(Cli::Parse({"-h"}).action == Cli::Action::Help,
            "cli: -h selects help");
        Check(Cli::Parse({"--nonsense"}).action == Cli::Action::UsageError,
            "cli: unknown option is a usage error");
        Check(Cli::Parse({"account-only"}).action == Cli::Action::UsageError,
            "cli: single non-option argument is a usage error");
        const Cli::Parsed auth = Cli::Parse({"2026123456", "测试密码"});
        Check(auth.action == Cli::Action::OneShotAuth &&
              auth.account == "2026123456" && auth.password == "测试密码",
            "cli: account and password select one-shot auth");
        Check(Cli::Parse({"--uninstall", "extra"}).action == Cli::Action::UsageError,
            "cli: option plus extra argument is a usage error");
        Check(Cli::Parse({"a", "b", "c"}).action == Cli::Action::UsageError,
            "cli: too many arguments is a usage error");
        const Cli::Parsed enableAuto = Cli::Parse({"--auto", "true"});
        Check(enableAuto.action == Cli::Action::SetAutoAuth &&
              enableAuto.autoAuthEnabled,
            "cli: --auto true selects enabling auto auth");
        const Cli::Parsed disableAuto = Cli::Parse({"--auto", "FALSE"});
        Check(disableAuto.action == Cli::Action::SetAutoAuth &&
              !disableAuto.autoAuthEnabled,
            "cli: --auto false selects disabling auto auth");
        Check(Cli::Parse({"--auto"}).action == Cli::Action::UsageError,
            "cli: --auto without value is a usage error");
        Check(Cli::Parse({"--auto", "yes"}).action == Cli::Action::UsageError,
            "cli: --auto with invalid value is a usage error");
        std::ostringstream usage;
        Cli::PrintUsage(usage);
        Check(usage.str().find("--uninstall") != std::string::npos,
            "cli: usage text mentions --uninstall");
        Check(usage.str().find("--auto") != std::string::npos,
            "cli: usage text mentions --auto");

        const std::vector<std::string> arguments = GetUtf8Arguments(argc, argv);
        bool roundTrip = arguments.size() ==
            static_cast<size_t>(argc > 0 ? argc - 1 : 0);
        for (size_t i = 0; roundTrip && i < arguments.size(); ++i)
            roundTrip = arguments[i] == argv[i + 1];
        Check(roundTrip, "cli: UTF-8 argument round trip without program name");
        Check(PrepareCliConsole(), "cli: console output can be prepared");
    }

    void TestAuthenticationResultUiPolicy()
    {
#ifdef _WIN32
        Check(AppUI::ShouldShowAuthenticationResult(),
            "Windows keeps the startup authentication result dialog");
#else
        Check(!AppUI::ShouldShowAuthenticationResult(),
            "non-Windows background authentication does not show a result dialog");
#endif
    }

    void TestUserDataFileLifecycle(const fs::path& root)
    {
        const fs::path userDataPath = root / "userdata.txt";
        std::string error;
        Check(UserData::CreateTemplate(userDataPath, error),
            "userdata template creation");
        const std::string templateBody = ReadAll(userDataPath);
        Check(templateBody.find("校园卡号") != std::string::npos &&
              templateBody.find("统一身份认证平台密码") != std::string::npos,
            "userdata template is flushed before create returns");

        std::string account;
        std::string password;
        Check(!UserData::ReadCredentials(userDataPath, account, password, error),
            "userdata template is rejected as credentials");

        {
            std::ofstream output(userDataPath, std::ios::trunc);
            output << "2026123456\r\npassword-with-symbols&+#\r\n";
        }
        Check(UserData::ReadCredentials(userDataPath, account, password, error),
            "userdata credentials can be read");
        Check(account == "2026123456" && password == "password-with-symbols&+#",
            "userdata CRLF is normalized");
        Check(UserData::RemoveFile(userDataPath, error),
            "userdata can be deleted after reading");
        Check(!fs::exists(userDataPath),
            "userdata is absent after deletion");
    }
    void TestPathsAndLogging(const fs::path& root)
    {
        const fs::path dataRoot = root / "data";
        const fs::path desktopRoot = root / "desktop";
        SetEnvironment("AUTOSZUWEB_DATA_ROOT", dataRoot.u8string());
        SetEnvironment("AUTOSZUWEB_DESKTOP_ROOT", desktopRoot.u8string());

        Check(GetUsersFolderPath() == dataRoot, "data root override");
        Check(GetDesktopPath() == desktopRoot, "desktop root override");
        const fs::path config = GetConfigPath();
        Check(config == dataRoot / "AutoSZUWeb" / "setting.json", "config path layout");
        Check(fs::is_directory(config.parent_path()), "config directory creation");

        WriteAuthLog("UnitTest", true, "127.0.0.1", "测试详情");
        const fs::path logDirectory = dataRoot / "AutoSZUWeb" / "logs";
        bool found = false;
        for (const auto& entry : fs::directory_iterator(logDirectory))
        {
            const std::string body = ReadAll(entry.path());
            if (body.find("方式=UnitTest") != std::string::npos &&
                body.find("结果=成功") != std::string::npos &&
                body.find("IP=127.0.0.1") != std::string::npos)
                found = true;
        }
        Check(found, "authentication log content");
    }

    void TestUninstallSharedData(const fs::path& root)
    {
        const fs::path dataRoot = root / "uninstall-data";
        const fs::path desktopRoot = root / "uninstall-desktop";
        SetEnvironment("AUTOSZUWEB_DATA_ROOT", dataRoot.u8string());
        SetEnvironment("AUTOSZUWEB_DESKTOP_ROOT", desktopRoot.u8string());

        const fs::path dataDirectory = dataRoot / "AutoSZUWeb";
        fs::create_directories(dataDirectory / "logs");
        {
            std::ofstream output(dataDirectory / "setting.json");
            output << "{}";
            std::ofstream log(dataDirectory / "logs" / "auth_2026-01-01.log");
            log << "entry";
            std::ofstream lock(dataDirectory / "AutoSZUWeb.instance.lock");
            lock << "1";
        }
        fs::create_directories(desktopRoot);
        {
            std::ofstream output(desktopRoot / "userdata.txt");
            output << "2026123456\r\npassword\r\n";
        }

        std::vector<fs::path> leftovers;
        Uninstall::RemoveSharedData(leftovers);
        Check(leftovers.empty(), "uninstall: shared data removal has no leftovers");
        Check(!fs::exists(dataDirectory / "setting.json"),
            "uninstall: setting.json removed");
        Check(!fs::exists(dataDirectory / "logs"),
            "uninstall: logs directory removed");
        Check(!fs::exists(dataDirectory / "AutoSZUWeb.instance.lock"),
            "uninstall: instance lock file removed");
        Check(!fs::exists(desktopRoot / "userdata.txt"),
            "uninstall: desktop userdata.txt removed");
        Check(!fs::exists(dataDirectory),
            "uninstall: data directory removed");
    }

    void TestAutoAuthConfig(const fs::path& root)
    {
        const fs::path dataRoot = root / "autoauth-data";
        SetEnvironment("AUTOSZUWEB_DATA_ROOT", dataRoot.u8string());
        const fs::path config = GetConfigPath();
        Check(!config.empty() && fs::is_directory(config.parent_path()),
            "auto-auth: config directory available");
        {
            std::ofstream output(config, std::ios::trunc);
            output << nlohmann::json{
                {"Account", "encrypted-account"},
                {"Password", "encrypted-password"}
            }.dump(2);
        }

        Check(Config::ReadAutoAuthEnabled(config),
            "auto-auth: missing field defaults to enabled");
        Check(Config::ReadAutoAuthEnabled(root / "missing.json"),
            "auto-auth: missing file defaults to enabled");

        std::string error;
        Check(Config::WriteAutoAuthEnabled(config, false, error),
            "auto-auth: disabling writes config");
        const nlohmann::json updated = nlohmann::json::parse(ReadAll(config));
        Check(updated.value("Account", "") == "encrypted-account" &&
              updated.value("Password", "") == "encrypted-password",
            "auto-auth: existing fields are preserved");
        Check(updated.value("AutoAuth", true) == false,
            "auto-auth: AutoAuth field is written");
        Check(!Config::ReadAutoAuthEnabled(config),
            "auto-auth: disabled value round trips");

        Check(Config::WriteAutoAuthEnabled(config, true, error),
            "auto-auth: enabling writes config");
        Check(Config::ReadAutoAuthEnabled(config),
            "auto-auth: enabled value round trips");
        Check(!fs::exists(fs::path(config) += ".tmp"),
            "auto-auth: no temporary file is left behind");

        {
            std::ofstream output(config, std::ios::trunc);
            output << "{broken";
        }
        error.clear();
        Check(!Config::WriteAutoAuthEnabled(config, true, error) && !error.empty(),
            "auto-auth: corrupt config is rejected");
        error.clear();
        Check(!Config::WriteAutoAuthEnabled(root / "missing.json", true, error),
            "auto-auth: missing config is rejected");
    }

    void TestLaunchAgentPlist()
    {
        const std::string plist = PlatformSupport::BuildLaunchAgentPlist(
            "/Applications/A&B.app/Contents/MacOS/AutoSZUWeb",
            "/tmp/a<b.out", "/tmp/a>b.err");
        Check(plist.find("com.autoszuweb.AutoSZUWeb") != std::string::npos,
            "LaunchAgent label");
        Check(plist.find("A&amp;B.app") != std::string::npos,
            "LaunchAgent executable XML escaping");
        Check(plist.find("a&lt;b.out") != std::string::npos,
            "LaunchAgent log XML escaping");
        Check(plist.find("LimitLoadToSessionType") != std::string::npos,
            "LaunchAgent restricted to Aqua session");
    }

    void TestSingleInstanceGuard(const fs::path& root)
    {
        const fs::path lockPath = root / "single-instance.lock";
        Runtime::SingleInstanceGuard first(lockPath);
        Check(first.IsPrimary(), "first instance acquires application lock");

        Runtime::SingleInstanceGuard second(lockPath);
        Check(!second.IsPrimary(), "second instance is rejected while first is alive");

        {
            Runtime::SingleInstanceGuard temporary(root / "released-instance.lock");
            Check(temporary.IsPrimary(), "temporary instance acquires lock");
        }
        Runtime::SingleInstanceGuard afterRelease(root / "released-instance.lock");
        Check(afterRelease.IsPrimary(), "instance lock is released on destruction");
    }

    void TestRuntimeStateMachine()
    {
        int loginCalls = 0;
        int networkCalls = 0;
        Runtime::MonitorCallbacks callbacks;
        callbacks.login = [&] { ++loginCalls; return true; };
        callbacks.networkCheck = [&] { ++networkCalls; return false; };

        auto state = Runtime::RunIteration(Runtime::ConnectionState::Online, callbacks);
        Check(state == Runtime::ConnectionState::Offline && networkCalls == 1,
            "online state changes to offline after failed probe");
        state = Runtime::RunIteration(state, callbacks);
        Check(state == Runtime::ConnectionState::Online && loginCalls == 1,
            "offline state changes to online after successful login");

        int sleeps = 0;
        int checks = 0;
        callbacks.networkCheck = [&] { ++checks; return true; };
        callbacks.sleep = [&](std::chrono::milliseconds duration) {
            Check(duration == std::chrono::milliseconds(25), "monitor interval propagation");
            ++sleeps;
        };
        callbacks.shouldStop = [&] { return checks >= 3; };
        Runtime::RunMonitorLoop(Runtime::ConnectionState::Online,
            callbacks, std::chrono::milliseconds(25));
        Check(checks == 3 && sleeps == 2, "monitor loop stop and sleep behavior");
    }

    void TestLoopbackProbe()
    {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            Check(false, "loopback socket startup");
            return;
        }
#endif
        TestSocket server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server == kInvalidTestSocket)
        {
            Check(false, "loopback server creation");
            return;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        bool setup = bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0
            && listen(server, 1) == 0;
#ifdef _WIN32
        int addressLength = sizeof(address);
#else
        socklen_t addressLength = sizeof(address);
#endif
        setup = setup && getsockname(server,
            reinterpret_cast<sockaddr*>(&address), &addressLength) == 0;
        Check(setup, "loopback server setup");
        if (setup)
            Check(ProbeTcpEndpoint("127.0.0.1", ntohs(address.sin_port), 1000),
                "cross-platform TCP probe reaches loopback server");
        Check(!ProbeTcpEndpoint("not-an-ip", 53, 10), "TCP probe rejects invalid address");
#ifdef _WIN32
        closesocket(server);
        WSACleanup();
#else
        close(server);
#endif
    }
}

int main(int argc, char* argv[])
{
    const fs::path root = fs::temp_directory_path() /
        ("AutoSZUWebTests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);

    try
    {
        TestEncryption();
        TestCli(argc, argv);
        TestAuthenticationResultUiPolicy();
        TestUserDataFileLifecycle(root);
        TestPathsAndLogging(root);
        TestUninstallSharedData(root);
        TestAutoAuthConfig(root);
#ifdef __APPLE__
        const fs::path launchAgentRoot = root / "LaunchAgents";
        SetEnvironment("AUTOSZUWEB_LAUNCH_AGENT_DIR", launchAgentRoot.u8string());
        SetAutoStart();
        const fs::path launchAgent = launchAgentRoot / "com.autoszuweb.AutoSZUWeb.plist";
        Check(fs::is_regular_file(launchAgent), "macOS LaunchAgent file creation");
        Check(ReadAll(launchAgent).find("AutoSZUWebTests") != std::string::npos,
            "macOS LaunchAgent uses current executable path");
        RemoveAutoStart();
        Check(!fs::exists(launchAgent), "macOS LaunchAgent removal");
        // 钥匙串删除仅在设置了专用服务名（CI 环境）时验证，避免误删本机真实密钥。
        if (std::getenv("AUTOSZUWEB_KEYCHAIN_SERVICE"))
        {
            std::string credentialError;
            const std::string beforeRemoval = EncryptStr("卸载前密文");
            Check(!beforeRemoval.empty(), "macOS keychain key exists before removal");
            Check(RemoveCredentialKey(credentialError),
                "macOS keychain credential key removal");
            Check(DecryptStr(beforeRemoval).empty(),
                "ciphertext is undecryptable after key removal");
            const std::string recreated = EncryptStr("卸载后密文");
            Check(!recreated.empty() && DecryptStr(recreated) == "卸载后密文",
                "macOS credential key is recreated after removal");
        }
#endif
        TestLaunchAgentPlist();
        TestSingleInstanceGuard(root);
        TestRuntimeStateMachine();
        TestLoopbackProbe();
    }
    catch (const std::exception& error)
    {
        std::cerr << "[EXCEPTION] " << error.what() << '\n';
        ++failures;
    }

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    std::cout << (failures == 0 ? "All tests passed.\n" : "Tests failed.\n");
    return failures == 0 ? 0 : 1;
}
