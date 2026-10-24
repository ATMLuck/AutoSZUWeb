#include "app_ui.h"
#include "sys.h"
#include "credential_crypto.h"
#include "file_path.h"
#include "platform_support.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <dpapi.h>
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <unistd.h>
#elif defined(__linux__) && defined(AUTOSZUWEB_LINUX_TEST)
#include <array>
#include <unistd.h>
#else
#error "Unsupported platform"
#endif

namespace fs = std::filesystem;

namespace
{
#ifdef __APPLE__
    const char* kMacKeychainService = "com.autoszuweb.AutoSZUWeb";
    const char* kMacKeychainAccount = "credential-encryption-key";

    const char* MacKeychainService()
    {
        const char* overrideService = std::getenv("AUTOSZUWEB_KEYCHAIN_SERVICE");
        return (overrideService && *overrideService)
            ? overrideService : kMacKeychainService;
    }
#endif

#if defined(__APPLE__) || (defined(__linux__) && defined(AUTOSZUWEB_LINUX_TEST))
#ifdef __APPLE__
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
#endif

    std::vector<unsigned char> PlatformEncryptionKey()
    {
#ifdef __APPLE__
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
#else
        // Linux 仅用于 CI/开发期逻辑测试，不用于发布或真实凭据保护。
        constexpr std::array<unsigned char, CredentialCrypto::kAesKeySize> kTestKey = {
            0x41,0x75,0x74,0x6f,0x53,0x5a,0x55,0x57,
            0x65,0x62,0x2d,0x4c,0x69,0x6e,0x75,0x78,
            0x2d,0x54,0x65,0x73,0x74,0x2d,0x4b,0x65,
            0x79,0x2d,0x30,0x30,0x30,0x30,0x30,0x31
        };
        CredentialCrypto::ClearError();
        return {kTestKey.begin(), kTestKey.end()};
#endif
    }

    std::string EncryptAesGcm(const std::string& plaintext)
    {
        std::vector<unsigned char> key = PlatformEncryptionKey();
        if (key.size() != CredentialCrypto::kAesKeySize)
            return {};
        return CredentialCrypto::EncryptAesGcm(key, plaintext);
    }

    std::string DecryptAesGcm(const std::string& encoded)
    {
        std::vector<unsigned char> key = PlatformEncryptionKey();
        if (key.size() != CredentialCrypto::kAesKeySize)
            return {};
        return CredentialCrypto::DecryptAesGcm(key, encoded);
    }
#endif

#ifdef _WIN32
    std::wstring Utf8ToWide(const std::string& text)
    {
        if (text.empty())
            return {};
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0)
            return {};
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }
#elif defined(__APPLE__)
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
#endif

    void FillTimeBuffers(char* timeBuffer, size_t timeSize,
                         char* dateBuffer, size_t dateSize)
    {
#ifdef _WIN32
        SYSTEMTIME time{};
        GetLocalTime(&time);
        std::snprintf(timeBuffer, timeSize, "%04d-%02d-%02d %02d:%02d:%02d",
            time.wYear, time.wMonth, time.wDay,
            time.wHour, time.wMinute, time.wSecond);
        std::snprintf(dateBuffer, dateSize, "%04d-%02d-%02d",
            time.wYear, time.wMonth, time.wDay);
#else
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        std::strftime(timeBuffer, timeSize, "%Y-%m-%d %H:%M:%S", &local);
        std::strftime(dateBuffer, dateSize, "%Y-%m-%d", &local);
#endif
    }
}

#ifdef _WIN32
AppUI::Button AppUI::ShowMessage(const std::string& text, const std::string& title,
                          bool allowCancel)
{
    const int result = MessageBoxW(nullptr, Utf8ToWide(text).c_str(),
        Utf8ToWide(title).c_str(),
        (allowCancel ? MB_OKCANCEL : MB_OK) | MB_ICONINFORMATION);
    return allowCancel && result == IDCANCEL ? Button::Cancel : Button::Ok;
}
#elif defined(__linux__) && defined(AUTOSZUWEB_LINUX_TEST)
AppUI::Button AppUI::ShowMessage(const std::string& text, const std::string& title,
                          bool allowCancel)
{
    std::cout << "[" << title << "] " << text << std::endl;
    const char* response = std::getenv("AUTOSZUWEB_UI_RESPONSE");
    return allowCancel && response && std::string(response) == "cancel"
        ? Button::Cancel : Button::Ok;
}
#endif

bool AppUI::ShouldShowAuthenticationResult()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

void SetAutoStart()
{
#ifdef _WIN32
    WCHAR selfPath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, selfPath, MAX_PATH) == 0)
    {
        AppUI::ShowMessage("无法获取程序路径");
        return;
    }

    WCHAR appdataPath[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdataPath)))
    {
        AppUI::ShowMessage("无法获取 AppData 路径");
        return;
    }

    std::wstring destinationDirectory = std::wstring(appdataPath) + L"\\AutoSZUWeb";
    CreateDirectoryW(destinationDirectory.c_str(), nullptr);
    std::wstring destinationPath = destinationDirectory + L"\\AutoSZUWeb.exe";
    if (!CopyFileW(selfPath, destinationPath.c_str(), FALSE) &&
        std::wstring(selfPath) != destinationPath)
    {
        AppUI::ShowMessage("复制程序到 AppData 失败");
        return;
    }

    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &key);
    if (openResult != ERROR_SUCCESS)
    {
        AppUI::ShowMessage("打开注册表失败");
        return;
    }
    const LONG writeResult = RegSetValueExW(key, L"AutoSZUWeb", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(destinationPath.c_str()),
        static_cast<DWORD>((destinationPath.size() + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    if (writeResult != ERROR_SUCCESS)
        AppUI::ShowMessage("写入注册表失败");
#elif defined(__APPLE__)
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
#elif defined(__linux__) && defined(AUTOSZUWEB_LINUX_TEST)
    // Linux 分支仅为开发期编译与测试桩。
    return;
#endif
}

std::string EncryptStr(const std::string& plaintext)
{
#ifdef _WIN32
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    input.cbData = static_cast<DWORD>(plaintext.size());
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"AutoSZUWeb", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        CredentialCrypto::SetError("Windows DPAPI 加密失败，错误码=" + std::to_string(GetLastError()));
        return {};
    }
    std::string encoded = CredentialCrypto::Base64Encode(output.pbData, output.cbData);
    LocalFree(output.pbData);
    CredentialCrypto::ClearError();
    return encoded;
#else
    return EncryptAesGcm(plaintext);
#endif
}

std::string DecryptStr(const std::string& ciphertext)
{
#ifdef _WIN32
    std::vector<unsigned char> decoded = CredentialCrypto::Base64Decode(ciphertext);
    if (decoded.empty())
    {
        CredentialCrypto::SetError("Windows DPAPI 密文 Base64 格式无效");
        return {};
    }
    DATA_BLOB input{};
    input.pbData = decoded.data();
    input.cbData = static_cast<DWORD>(decoded.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        CredentialCrypto::SetError("Windows DPAPI 解密失败，错误码=" + std::to_string(GetLastError()));
        return {};
    }
    std::string plaintext(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    CredentialCrypto::ClearError();
    return plaintext;
#else
    return DecryptAesGcm(ciphertext);
#endif
}

std::string GetCredentialError()
{
    return CredentialCrypto::GetError();
}

void WriteAuthLog(const std::string& method, bool success,
                  const std::string& deviceIp, const std::string& message)
{
    char timeBuffer[32] = {};
    char dateBuffer[16] = {};
    FillTimeBuffers(timeBuffer, sizeof(timeBuffer), dateBuffer, sizeof(dateBuffer));

    fs::path logDirectory = GetUsersFolderPath() / "AutoSZUWeb" / "logs";
    std::error_code error;
    fs::create_directories(logDirectory, error);
    std::ofstream log(logDirectory /
        ("auth_" + std::string(dateBuffer) + ".log"), std::ios::app);
    if (!log)
        return;

    log << "[" << timeBuffer << "] 方式=" << method
        << " 结果=" << (success ? "成功" : "失败");
    if (success)
    {
        if (!deviceIp.empty())
            log << " IP=" << deviceIp;
        if (!message.empty())
            log << " 详情=" << message;
    }
    else
    {
        log << " 原因=" << message;
    }
    log << '\n';
}
