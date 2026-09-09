#include "app_ui.h"
#include "web.h"
#include "srun.h"
#include "sys.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <string>

bool IsFirstBoot();

namespace
{
    const std::string kLoginUrlBase =
        "http://172.30.255.42:801/eportal/portal/login";

    size_t WriteCallback(void* contents, size_t size, size_t count, void* userData)
    {
        static_cast<std::string*>(userData)->append(
            static_cast<char*>(contents), size * count);
        return size * count;
    }

    bool LoginDormitory(const std::string& account, const std::string& password,
                        std::string& message, std::string& note)
    {
        CURL* curl = curl_easy_init();
        if (!curl)
        {
            message = "curl 初始化失败";
            return false;
        }

        char* escapedAccount = curl_easy_escape(curl, account.c_str(),
            static_cast<int>(account.size()));
        char* escapedPassword = curl_easy_escape(curl, password.c_str(),
            static_cast<int>(password.size()));
        if (!escapedAccount || !escapedPassword)
        {
            if (escapedAccount) curl_free(escapedAccount);
            if (escapedPassword) curl_free(escapedPassword);
            curl_easy_cleanup(curl);
            message = "宿舍区请求参数编码失败";
            return false;
        }

        const std::string fullUrl = kLoginUrlBase
            + "?user_account=%2C0%2C" + escapedAccount
            + "&user_password=" + escapedPassword;
        curl_free(escapedAccount);
        curl_free(escapedPassword);

        std::string responseBody;
        curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        const CURLcode result = curl_easy_perform(curl);
        bool success = false;
        if (result != CURLE_OK)
        {
            message = "宿舍区请求失败: " + std::string(curl_easy_strerror(result));
        }
        else
        {
            const size_t start = responseBody.find('(');
            const size_t end = responseBody.rfind(')');
            if (start != std::string::npos && end != std::string::npos && end > start)
            {
                try
                {
                    nlohmann::json response = nlohmann::json::parse(
                        responseBody.substr(start + 1, end - start - 1));
                    const int responseResult = response.value("result", 1);
                    const std::string responseMessage = response.value("msg", "");
                    const bool resultOk = responseResult == 0;
                    const bool messageOk =
                        responseMessage.find("认证成功") != std::string::npos;
                    success = resultOk || messageOk;
                    if (success && !resultOk)
                        note = "服务器回 result=" + std::to_string(responseResult)
                            + " 原始响应: " + responseBody;
                    message = responseMessage.empty()
                        ? (success ? "宿舍区认证成功" : "宿舍区认证失败")
                        : responseMessage;
                }
                catch (const std::exception&)
                {
                    message = "宿舍区响应 JSON 解析失败";
                }
            }
            else
            {
                message = responseBody.empty() ? "宿舍区返回空响应" : responseBody;
            }
        }

        curl_easy_cleanup(curl);
        return success;
    }
}

bool Login(const std::string& account, const std::string& password)
{
    static const bool curlInitialized = curl_global_init(CURL_GLOBAL_ALL) == CURLE_OK;
    if (!curlInitialized)
    {
        if (IsFirstBoot())
            AppUI::ShowMessage("curl 全局初始化失败");
        WriteAuthLog("初始化", false, "", "curl 全局初始化失败");
        return false;
    }

    std::string srunMessage;
    std::string dormitoryMessage;
    std::string method;
    std::string successMessage;
    std::string logNote;

    bool ok = SrunLogin(account, password, srunMessage);
    if (ok)
    {
        method = "SRun(教学区)";
        successMessage = srunMessage;
    }
    else
    {
        ok = LoginDormitory(account, password, dormitoryMessage, logNote);
        method = ok ? "ePortal(宿舍区)" : "SRun→ePortal";
        if (ok)
            successMessage = dormitoryMessage;
    }

    std::string logReason;
    if (!ok)
        logReason = "教学区: " + (srunMessage.empty() ? "无" : srunMessage)
            + "; 宿舍区: " + (dormitoryMessage.empty() ? "无" : dormitoryMessage);
    WriteAuthLog(method, ok, ok ? GetLocalIp() : "", ok ? logNote : logReason);

    if (IsFirstBoot())
    {
        if (ok)
            AppUI::ShowMessage(successMessage, "提示");
        else
            AppUI::ShowMessage(
                "教学区认证失败: " + (srunMessage.empty() ? "无" : srunMessage)
                + "\n宿舍区认证失败: "
                + (dormitoryMessage.empty() ? "无" : dormitoryMessage),
                "提示");
    }
    return ok;
}
