#include "sys.h"

#include "app_ui.h"
#include "credential_crypto.h"

#include <string>
#include <vector>

#include <windows.h>
#include <shlobj.h>
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
