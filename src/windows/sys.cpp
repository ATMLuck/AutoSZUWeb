#include "sys.h"

#include "app_ui.h"
#include "credential_crypto.h"

#include <string>
#include <vector>

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dpapi.h>
#include <wincrypt.h>

void SetAutoStart()
{
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
}

std::vector<std::string> GetUtf8Arguments(int, char**)
{
    int wideCount = 0;
    LPWSTR* wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (!wideArguments)
        return {};

    std::vector<std::string> arguments;
    arguments.reserve(static_cast<size_t>(wideCount > 1 ? wideCount - 1 : 0));
    for (int i = 1; i < wideCount; ++i)
    {
        const int length = WideCharToMultiByte(CP_UTF8, 0,
            wideArguments[i], lstrlenW(wideArguments[i]), nullptr, 0, nullptr, nullptr);
        if (length <= 0)
        {
            arguments.emplace_back();
            continue;
        }
        std::string converted(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideArguments[i], lstrlenW(wideArguments[i]),
            converted.data(), length, nullptr, nullptr);
        arguments.push_back(std::move(converted));
    }
    LocalFree(wideArguments);
    return arguments;
}

bool PrepareCliConsole()
{
    // stdout 已连接管道或文件（Git Bash、重定向、IDE）时直接使用。
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output && output != INVALID_HANDLE_VALUE &&
        GetFileType(output) != FILE_TYPE_UNKNOWN)
        return true;

    // 从 cmd/PowerShell 启动时附加其控制台；双击启动则无控制台可用。
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return false;
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    return true;
}

std::string EncryptStr(const std::string& plaintext)
{
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
}

std::string DecryptStr(const std::string& ciphertext)
{
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
}
