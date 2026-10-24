#include "sys.h"

#include "credential_crypto.h"
#include "file_path.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
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
