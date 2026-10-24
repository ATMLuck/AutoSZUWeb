#include "sys.h"

#include "app_ui.h"
#include "credential_crypto.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dpapi.h>
#include <wincrypt.h>

namespace fs = std::filesystem;

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

void RemoveAutoStart()
{
    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &key);
    if (openResult != ERROR_SUCCESS)
        return;
    // 值不存在（ERROR_FILE_NOT_FOUND）视为成功：本来就没有自启动。
    RegDeleteValueW(key, L"AutoSZUWeb");
    RegCloseKey(key);
}

bool RemoveCredentialKey(std::string&)
{
    // DPAPI 密文保存在配置文件内，无独立密钥可删。
    return true;
}

namespace
{
    // 运行中的 exe 无法直接删除；派生独立 cmd 进程在本进程退出后延迟删除，
    // 派生失败时回退为重启后删除。ping 用于等待（timeout 依赖控制台输入）。
    void ScheduleDelayedDeletion(const std::wstring& path)
    {
        std::wstring command =
            L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + path + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        {
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
            return;
        }
        MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
}

bool RemovePlatformResidue(std::vector<fs::path>& leftovers, std::string&)
{
    WCHAR appdataPath[MAX_PATH] = {};
    const bool hasAppdata = SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA,
        nullptr, 0, appdataPath));
    if (hasAppdata)
    {
        // 早期版本遗留在数据根目录下的旧配置。
        std::error_code error;
        fs::remove(fs::path(appdataPath) / "autoWEB.json", error);
    }

    // 程序本体：正在运行的 exe 由延迟删除接管（本进程退出后生效）。
    fs::path runningExecutable;
    WCHAR selfPath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, selfPath, MAX_PATH) != 0)
    {
        runningExecutable = fs::path(selfPath);
        ScheduleDelayedDeletion(runningExecutable.wstring());
        leftovers.erase(std::remove(leftovers.begin(), leftovers.end(),
                          runningExecutable),
            leftovers.end());
    }

    // AppData 自启动副本：仍存在且不是运行中的 exe 时（被其他后台实例占用），
    // 直接删除或安排延迟删除，并保留在 leftovers 中提示用户。
    if (hasAppdata && !runningExecutable.empty())
    {
        const fs::path autostartCopy =
            fs::path(appdataPath) / "AutoSZUWeb" / "AutoSZUWeb.exe";
        std::error_code error;
        if (fs::exists(autostartCopy, error) && autostartCopy != runningExecutable)
        {
            fs::remove(autostartCopy, error);
            if (error || fs::exists(autostartCopy, error))
                ScheduleDelayedDeletion(autostartCopy.wstring());
        }
    }
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
