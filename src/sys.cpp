#include "app_ui.h"
#include "sys.h"
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
    constexpr size_t kAesKeySize = 32;
    constexpr size_t kGcmIvSize = 12;
    constexpr size_t kGcmTagSize = 16;
#ifdef __APPLE__
    const char* kMacKeychainService = "com.autoszuweb.AutoSZUWeb";
    const char* kMacKeychainAccount = "credential-encryption-key";
#endif

    std::string Base64Encode(const unsigned char* data, size_t length)
    {
        if (length == 0)
            return {};
        std::vector<unsigned char> output(4 * ((length + 2) / 3) + 1);
        const int outputLength = EVP_EncodeBlock(output.data(), data,
            static_cast<int>(length));
        return outputLength > 0
            ? std::string(reinterpret_cast<char*>(output.data()), outputLength)
            : std::string{};
    }

    std::vector<unsigned char> Base64Decode(const std::string& encoded)
    {
        if (encoded.empty())
            return {};
        std::vector<unsigned char> output((encoded.size() * 3) / 4 + 3);
        int length = EVP_DecodeBlock(output.data(),
            reinterpret_cast<const unsigned char*>(encoded.data()),
            static_cast<int>(encoded.size()));
        if (length < 0)
            return {};
        if (!encoded.empty() && encoded.back() == '=') --length;
        if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=') --length;
        output.resize(static_cast<size_t>(length));
        return output;
    }

#if defined(__APPLE__) || (defined(__linux__) && defined(AUTOSZUWEB_LINUX_TEST))
    std::vector<unsigned char> PlatformEncryptionKey()
    {
#ifdef __APPLE__
        const void* keys[] = {
            kSecClass, kSecAttrService, kSecAttrAccount,
            kSecUseDataProtectionKeychain, kSecReturnData, kSecMatchLimit
        };
        const void* values[] = {
            kSecClassGenericPassword,
            CFStringCreateWithCString(nullptr, kMacKeychainService, kCFStringEncodingUTF8),
            CFStringCreateWithCString(nullptr, kMacKeychainAccount, kCFStringEncodingUTF8),
            kCFBooleanTrue, kCFBooleanTrue, kSecMatchLimitOne
        };
        CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 6,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFTypeRef result = nullptr;
        OSStatus status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        CFRelease(values[1]);
        CFRelease(values[2]);

        if (status == errSecSuccess && result)
        {
            CFDataRef data = static_cast<CFDataRef>(result);
            std::vector<unsigned char> key(CFDataGetBytePtr(data),
                CFDataGetBytePtr(data) + CFDataGetLength(data));
            CFRelease(result);
            if (key.size() == kAesKeySize)
                return key;
        }
        else if (result)
        {
            CFRelease(result);
        }
        if (status != errSecItemNotFound)
            return {};

        std::vector<unsigned char> key(kAesKeySize);
        if (SecRandomCopyBytes(kSecRandomDefault, key.size(), key.data()) != errSecSuccess)
            return {};

        CFDataRef keyData = CFDataCreate(nullptr, key.data(), key.size());
        const void* addKeys[] = {
            kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData,
            kSecUseDataProtectionKeychain, kSecAttrAccessible, kSecAttrSynchronizable
        };
        CFStringRef service = CFStringCreateWithCString(nullptr,
            kMacKeychainService, kCFStringEncodingUTF8);
        CFStringRef account = CFStringCreateWithCString(nullptr,
            kMacKeychainAccount, kCFStringEncodingUTF8);
        const void* addValues[] = {
            kSecClassGenericPassword, service, account, keyData,
            kCFBooleanTrue, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly, kCFBooleanFalse
        };
        CFDictionaryRef addQuery = CFDictionaryCreate(nullptr, addKeys, addValues, 7,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        status = SecItemAdd(addQuery, nullptr);
        CFRelease(addQuery);
        CFRelease(service);
        CFRelease(account);
        CFRelease(keyData);

        if (status == errSecDuplicateItem)
        {
            std::vector<unsigned char> existingKey = PlatformEncryptionKey();
            return existingKey;
        }
        return status == errSecSuccess ? key : std::vector<unsigned char>{};
#else
        // Linux 仅用于 CI/开发期逻辑测试，不用于发布或真实凭据保护。
        constexpr std::array<unsigned char, kAesKeySize> kTestKey = {
            0x41,0x75,0x74,0x6f,0x53,0x5a,0x55,0x57,
            0x65,0x62,0x2d,0x4c,0x69,0x6e,0x75,0x78,
            0x2d,0x54,0x65,0x73,0x74,0x2d,0x4b,0x65,
            0x79,0x2d,0x30,0x30,0x30,0x30,0x30,0x31
        };
        return {kTestKey.begin(), kTestKey.end()};
#endif
    }

    std::string EncryptAesGcm(const std::string& plaintext)
    {
        std::vector<unsigned char> key = PlatformEncryptionKey();
        if (key.size() != kAesKeySize)
            return {};

        std::vector<unsigned char> iv(kGcmIvSize);
        if (RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1)
            return {};

        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (!context)
            return {};

        std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
        int length = 0;
        int totalLength = 0;
        bool ok =
            EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
                static_cast<int>(iv.size()), nullptr) == 1 &&
            EVP_EncryptInit_ex(context, nullptr, nullptr, key.data(), iv.data()) == 1 &&
            EVP_EncryptUpdate(context, ciphertext.data(), &length,
                reinterpret_cast<const unsigned char*>(plaintext.data()),
                static_cast<int>(plaintext.size())) == 1;
        totalLength = length;
        if (ok)
            ok = EVP_EncryptFinal_ex(context, ciphertext.data() + totalLength, &length) == 1;
        totalLength += length;

        std::vector<unsigned char> tag(kGcmTagSize);
        if (ok)
            ok = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG,
                static_cast<int>(tag.size()), tag.data()) == 1;
        EVP_CIPHER_CTX_free(context);
        if (!ok)
            return {};

        ciphertext.resize(static_cast<size_t>(totalLength));
        std::vector<unsigned char> payload;
        payload.reserve(iv.size() + ciphertext.size() + tag.size());
        payload.insert(payload.end(), iv.begin(), iv.end());
        payload.insert(payload.end(), ciphertext.begin(), ciphertext.end());
        payload.insert(payload.end(), tag.begin(), tag.end());
        return "v1:" + Base64Encode(payload.data(), payload.size());
    }

    std::string DecryptAesGcm(const std::string& encoded)
    {
        if (encoded.rfind("v1:", 0) != 0)
            return {};
        std::vector<unsigned char> payload = Base64Decode(encoded.substr(3));
        if (payload.size() < kGcmIvSize + kGcmTagSize)
            return {};

        std::vector<unsigned char> key = PlatformEncryptionKey();
        if (key.size() != kAesKeySize)
            return {};

        const unsigned char* iv = payload.data();
        const size_t ciphertextLength = payload.size() - kGcmIvSize - kGcmTagSize;
        const unsigned char* ciphertext = payload.data() + kGcmIvSize;
        unsigned char* tag = payload.data() + kGcmIvSize + ciphertextLength;

        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (!context)
            return {};
        std::vector<unsigned char> plaintext(ciphertextLength + EVP_MAX_BLOCK_LENGTH);
        int length = 0;
        int totalLength = 0;
        bool ok =
            EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
                static_cast<int>(kGcmIvSize), nullptr) == 1 &&
            EVP_DecryptInit_ex(context, nullptr, nullptr, key.data(), iv) == 1 &&
            EVP_DecryptUpdate(context, plaintext.data(), &length, ciphertext,
                static_cast<int>(ciphertextLength)) == 1;
        totalLength = length;
        if (ok)
            ok = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG,
                static_cast<int>(kGcmTagSize), tag) == 1;
        if (ok)
            ok = EVP_DecryptFinal_ex(context, plaintext.data() + totalLength, &length) == 1;
        totalLength += length;
        EVP_CIPHER_CTX_free(context);
        if (!ok)
            return {};
        return std::string(reinterpret_cast<char*>(plaintext.data()), totalLength);
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
            buffer.assign(static_cast<size_t>(size) + 1, \0);
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
        return {};
    std::string encoded = Base64Encode(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return encoded;
#else
    return EncryptAesGcm(plaintext);
#endif
}

std::string DecryptStr(const std::string& ciphertext)
{
#ifdef _WIN32
    std::vector<unsigned char> decoded = Base64Decode(ciphertext);
    if (decoded.empty())
        return {};
    DATA_BLOB input{};
    input.pbData = decoded.data();
    input.cbData = static_cast<DWORD>(decoded.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return {};
    std::string plaintext(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return plaintext;
#else
    return DecryptAesGcm(ciphertext);
#endif
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
