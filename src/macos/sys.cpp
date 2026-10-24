#include "sys.h"

#include "app_ui.h"
#include "credential_crypto.h"
#include "file_path.h"
#include "platform_support.h"

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
    fs::path plistPath = launchAgents / "com.autoszuweb.AutoSZUWeb.plist";
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
