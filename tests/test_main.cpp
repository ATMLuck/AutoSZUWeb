#include "file_path.h"
#include "network.h"
#include "platform_support.h"
#include "runtime.h"
#include "sys.h"
#include "user_data.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

int main()
{
    const fs::path root = fs::temp_directory_path() /
        ("AutoSZUWebTests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);

    try
    {
        TestEncryption();
        TestUserDataFileLifecycle(root);
        TestPathsAndLogging(root);
#ifdef __APPLE__
        const fs::path launchAgentRoot = root / "LaunchAgents";
        SetEnvironment("AUTOSZUWEB_LAUNCH_AGENT_DIR", launchAgentRoot.u8string());
        SetAutoStart();
        const fs::path launchAgent = launchAgentRoot / "com.autoszuweb.AutoSZUWeb.plist";
        Check(fs::is_regular_file(launchAgent), "macOS LaunchAgent file creation");
        Check(ReadAll(launchAgent).find("AutoSZUWebTests") != std::string::npos,
            "macOS LaunchAgent uses current executable path");
#endif
        TestLaunchAgentPlist();
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
