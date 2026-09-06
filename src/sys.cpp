#include "sys.h"
#include "file_path.h"
#include <windows.h>
#include <shlobj.h>
#include <dpapi.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdio>

void SetAutoStart()
{
    // 1. 获取当前 exe 路径
    WCHAR selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    std::wstring selfPathStr = selfPath;

    // 2. 获取 %AppData% 并创建目标目录
    WCHAR appdataPath[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdataPath);
    std::wstring destDir = std::wstring(appdataPath) + L"\\AutoSZUWeb";
    CreateDirectoryW(destDir.c_str(), NULL);

    // 3. 复制自身到 %AppData%\AutoSZUWeb
    std::wstring destPath = destDir + L"\\AutoSZUWeb.exe";
    if (!CopyFileW(selfPathStr.c_str(), destPath.c_str(), FALSE)) {
        // 已在目标运行则复制失败 (exe 正被占用), 跳过复制
        if (selfPathStr != destPath) {
            MessageBoxW(NULL, L"复制程序到AppData失败", L"提示", MB_OK);
            return;
        }
    }

    // 4. 写入注册表自启动
    HKEY hKey;
    LPCWSTR subKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_SET_VALUE, &hKey);
    if (result != ERROR_SUCCESS) {
        MessageBoxW(NULL, L"打开注册表失败", L"提示", MB_OK);
        return;
    }

    std::wstring appName = L"AutoSZUWeb";
    result = RegSetValueExW(hKey, appName.c_str(), 0, REG_SZ,
        (const BYTE*)destPath.c_str(), (destPath.size() + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) {
        MessageBoxW(NULL, L"写入注册表失败", L"提示", MB_OK);
    }
}

// DPAPI 加密: 明文 → Base64 密文
std::string EncryptStr(const std::string& plaintext)
{
    DATA_BLOB in, out;
    in.pbData = (BYTE*)plaintext.data();
    in.cbData = (DWORD)plaintext.size();

    if (!CryptProtectData(&in, L"AutoSZUWeb", NULL, NULL, NULL, 0, &out))
        return "";

    DWORD b64Len = 0;
    CryptBinaryToStringA(out.pbData, out.cbData,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &b64Len);
    std::string b64Str(b64Len, '\0');
    CryptBinaryToStringA(out.pbData, out.cbData,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &b64Str[0], &b64Len);
    while (!b64Str.empty() && b64Str.back() == '\0')
        b64Str.pop_back();

    LocalFree(out.pbData);
    return b64Str;
}

// DPAPI 解密: Base64 密文 → 明文
std::string DecryptStr(const std::string& ciphertext)
{
    if (ciphertext.empty()) return "";

    DWORD binLen = 0;
    CryptStringToBinaryA(ciphertext.data(), (DWORD)ciphertext.size(),
        CRYPT_STRING_BASE64, NULL, &binLen, NULL, NULL);
    std::vector<BYTE> binData(binLen);
    CryptStringToBinaryA(ciphertext.data(), (DWORD)ciphertext.size(),
        CRYPT_STRING_BASE64, binData.data(), &binLen, NULL, NULL);

    DATA_BLOB in, out;
    in.pbData = binData.data();
    in.cbData = binLen;

    if (!CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out))
        return "";

    std::string plaintext((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plaintext;
}

void WriteAuthLog(const std::string& method, bool success,
                  const std::string& deviceIp, const std::string& message)
{
    // 时间戳 YYYY-MM-DD HH:MM:SS, 文件名取日期按天分文件
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeBuf[32];
    std::snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    char dateBuf[16];
    std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
        st.wYear, st.wMonth, st.wDay);

    // 日志存放于 %APPDATA%\AutoSZUWeb\logs\auth_YYYY-MM-DD.log, 追加写
    fs::path logDir = GetUsersFolderPath() / "AutoSZUWeb" / "logs";
    std::error_code ec;
    fs::create_directories(logDir, ec);
    std::ofstream log(logDir / ("auth_" + std::string(dateBuf) + ".log"), std::ios::app);
    if (!log.is_open())
        return;

    log << "[" << timeBuf << "] 方式=" << method
        << " 结果=" << (success ? "成功" : "失败");
    if (success)
    {
        log << " IP=" << deviceIp;
        // 成功时若带 message(如重复认证的服务器原始响应)一并输出, 便于排查
        if (!message.empty())
            log << " 详情=" << message;
    }
    else
    {
        log << " 原因=" << message;
    }
    log << std::endl;
}