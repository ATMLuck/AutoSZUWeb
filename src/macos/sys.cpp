#include "sys.h"

#include "app_ui.h"
#include "credential_crypto.h"
#include "file_path.h"
#include "platform_support.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <Security/Security.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace
{
    const char* kMacKeychainService = "com.autoszuweb.AutoSZUWeb";
    const char* kMacKeychainAccount = "credential-encryption-key";
    const char* kMacLaunchAgentLabel = "com.autoszuweb.AutoSZUWeb";

    const char* MacKeychainService()
    {
        const char* overrideService = std::getenv("AUTOSZUWEB_KEYCHAIN_SERVICE");
        return (overrideService && *overrideService)
            ? overrideService : kMacKeychainService;
    }

    std::string MacStatusMessage(OSStatus status)
    {
        CFStringRef description = SecCopyErrorMessageString(status, nullptr);
        if (!description)
            return "OSStatus=" + std::to_string(status);

        char buffer[512] = {};
        const bool converted = CFStringGetCString(description, buffer,
            sizeof(buffer), kCFStringEncodingUTF8);
        CFRelease(description);
        return converted
            ? std::string(buffer) + " (OSStatus=" + std::to_string(status) + ")"
            : "OSStatus=" + std::to_string(status);
    }

    std::vector<unsigned char> ReadMacKeychainKey(OSStatus& status)
    {
        CFStringRef service = CFStringCreateWithCString(nullptr,
            MacKeychainService(), kCFStringEncodingUTF8);
        CFStringRef account = CFStringCreateWithCString(nullptr,
            kMacKeychainAccount, kCFStringEncodingUTF8);
        const void* keys[] = {
            kSecClass, kSecAttrService, kSecAttrAccount,
            kSecReturnData, kSecMatchLimit
        };
        const void* values[] = {
            kSecClassGenericPassword, service, account,
            kCFBooleanTrue, kSecMatchLimitOne
        };
        CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 5,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFTypeRef result = nullptr;
        status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        CFRelease(service);
        CFRelease(account);

        if (status != errSecSuccess)
        {
            if (result)
                CFRelease(result);
            return {};
        }
        if (!result || CFGetTypeID(result) != CFDataGetTypeID())
        {
            if (result)
                CFRelease(result);
            status = errSecDecode;
            return {};
        }

        CFDataRef data = static_cast<CFDataRef>(result);
        std::vector<unsigned char> key(CFDataGetBytePtr(data),
            CFDataGetBytePtr(data) + CFDataGetLength(data));
        CFRelease(result);
        if (key.size() != CredentialCrypto::kAesKeySize)
        {
            status = errSecDecode;
            return {};
        }
        return key;
    }

    std::vector<unsigned char> PlatformEncryptionKey()
    {
        OSStatus status = errSecSuccess;
        std::vector<unsigned char> existingKey = ReadMacKeychainKey(status);
        if (!existingKey.empty())
        {
            CredentialCrypto::ClearError();
            return existingKey;
        }
        if (status != errSecItemNotFound)
        {
            CredentialCrypto::SetError("读取 macOS 登录钥匙串失败：" + MacStatusMessage(status));
            return {};
        }

        std::vector<unsigned char> key(CredentialCrypto::kAesKeySize);
        status = SecRandomCopyBytes(kSecRandomDefault, key.size(), key.data());
        if (status != errSecSuccess)
        {
            CredentialCrypto::SetError("生成 macOS 凭据密钥失败：" + MacStatusMessage(status));
            return {};
        }

        CFStringRef service = CFStringCreateWithCString(nullptr,
            MacKeychainService(), kCFStringEncodingUTF8);
        CFStringRef account = CFStringCreateWithCString(nullptr,
            kMacKeychainAccount, kCFStringEncodingUTF8);
        CFDataRef keyData = CFDataCreate(nullptr, key.data(), key.size());
        const void* addKeys[] = {
            kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData
        };
        const void* addValues[] = {
            kSecClassGenericPassword, service, account, keyData
        };
        CFDictionaryRef addQuery = CFDictionaryCreate(nullptr,
            addKeys, addValues, 4,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        status = SecItemAdd(addQuery, nullptr);
        CFRelease(addQuery);
        CFRelease(service);
        CFRelease(account);
        CFRelease(keyData);

        if (status == errSecDuplicateItem)
        {
            existingKey = ReadMacKeychainKey(status);
            if (!existingKey.empty())
            {
                CredentialCrypto::ClearError();
                return existingKey;
            }
        }
        if (status != errSecSuccess)
        {
            CredentialCrypto::SetError("写入 macOS 登录钥匙串失败：" + MacStatusMessage(status));
            return {};
        }

        CredentialCrypto::ClearError();
        return key;
    }

    fs::path CurrentExecutablePath()
    {
        uint32_t size = PATH_MAX;
        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        {
            buffer.assign(static_cast<size_t>(size) + 1, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) != 0)
                return {};
        }
        std::error_code error;
        fs::path canonical = fs::weakly_canonical(fs::u8path(buffer.data()), error);
        return error ? fs::u8path(buffer.data()) : canonical;
    }
}

void SetAutoStart()
{
    fs::path executable = CurrentExecutablePath();
    fs::path home = std::getenv("HOME") ? fs::u8path(std::getenv("HOME")) : fs::path{};
    if (executable.empty() || home.empty())
    {
        AppUI::ShowMessage("无法获取程序路径或用户主目录，未能注册开机自启");
        return;
    }

    fs::path launchAgents = home / "Library" / "LaunchAgents";
    const char* launchAgentOverride = std::getenv("AUTOSZUWEB_LAUNCH_AGENT_DIR");
    if (launchAgentOverride && *launchAgentOverride)
        launchAgents = fs::u8path(launchAgentOverride);
    fs::path logDirectory = GetUsersFolderPath() / "AutoSZUWeb" / "logs";
    std::error_code error;
    fs::create_directories(launchAgents, error);
    fs::create_directories(logDirectory, error);
    fs::path plistPath =
        launchAgents / (std::string(kMacLaunchAgentLabel) + ".plist");
    std::ofstream plist(plistPath, std::ios::trunc);
    if (!plist)
    {
        AppUI::ShowMessage("无法写入 LaunchAgent 配置：" + plistPath.u8string());
        return;
    }
    plist << PlatformSupport::BuildLaunchAgentPlist(
        executable.u8string(),
        (logDirectory / "launchd.out.log").u8string(),
        (logDirectory / "launchd.err.log").u8string());
    plist.close();
    fs::permissions(plistPath,
        fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::replace, error);
}

std::vector<std::string> GetUtf8Arguments(int argc, char* argv[])
{
    // macOS 的 argv 本身就是 UTF-8 编码。
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
        arguments.push_back(argv[i] ? argv[i] : "");
    return arguments;
}

bool PrepareCliConsole()
{
    return true;
}

void RemoveAutoStart()
{
    fs::path home = std::getenv("HOME") ? fs::u8path(std::getenv("HOME")) : fs::path{};
    if (home.empty())
        return;
    fs::path launchAgents = home / "Library" / "LaunchAgents";
    const char* launchAgentOverride = std::getenv("AUTOSZUWEB_LAUNCH_AGENT_DIR");
    if (launchAgentOverride && *launchAgentOverride)
        launchAgents = fs::u8path(launchAgentOverride);
    std::error_code error;
    fs::remove(launchAgents / (std::string(kMacLaunchAgentLabel) + ".plist"), error);
}

bool RemoveCredentialKey(std::string& error)
{
    CFStringRef service = CFStringCreateWithCString(nullptr,
        MacKeychainService(), kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithCString(nullptr,
        kMacKeychainAccount, kCFStringEncodingUTF8);
    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
    const void* values[] = { kSecClassGenericPassword, service, account };
    CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 3,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    CFRelease(service);
    CFRelease(account);
    if (status != errSecSuccess && status != errSecItemNotFound)
    {
        error = "删除 macOS 登录钥匙串密钥失败：" + MacStatusMessage(status);
        return false;
    }
    return true;
}

bool RemovePlatformResidue(std::vector<fs::path>& leftovers, std::string& error)
{
    // 先卸载 LaunchAgent，顺带终止 launchd 拉起的后台实例，避免其继续重建
    // 日志目录。自身是 launchd 子进程时跳过（进程即将退出，下次登录 plist
    // 已删除不会再加载）。
    if (std::getppid() != 1)
    {
        const std::string command = "launchctl bootout gui/" +
            std::to_string(::getuid()) + "/" + kMacLaunchAgentLabel +
            " >/dev/null 2>&1";
        std::system(command.c_str());
    }

    if (!RemoveCredentialKey(error))
        return false;

    // 程序本体：.app 包（或裸二进制）；POSIX 允许删除正在运行的文件。
    const fs::path executable = CurrentExecutablePath();
    if (!executable.empty())
    {
        fs::path target = executable;
        const std::string native = executable.u8string();
        const size_t marker = native.find(".app/Contents/MacOS/");
        if (marker != std::string::npos)
            target = fs::u8path(native.substr(0, marker + 4)); // 含 ".app"
        std::error_code removeError;
        fs::remove_all(target, removeError);
        if (!removeError && !fs::exists(target, removeError))
            leftovers.erase(std::remove(leftovers.begin(), leftovers.end(), target),
                leftovers.end());
        else
            leftovers.push_back(target);
    }
    return true;
}

std::string EncryptStr(const std::string& plaintext)
{
    std::vector<unsigned char> key = PlatformEncryptionKey();
    if (key.size() != CredentialCrypto::kAesKeySize)
        return {};
    return CredentialCrypto::EncryptAesGcm(key, plaintext);
}

std::string DecryptStr(const std::string& ciphertext)
{
    std::vector<unsigned char> key = PlatformEncryptionKey();
    if (key.size() != CredentialCrypto::kAesKeySize)
        return {};
    return CredentialCrypto::DecryptAesGcm(key, ciphertext);
}
