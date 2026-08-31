#include "curl/curl.h"
#include "srun.h"
#include "sys.h"
#include <nlohmann/json.hpp>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <string>
#include <iostream>

extern bool FirstBoot;
const std::string LOGIN_URL_BASE = "http://172.30.255.42:801/eportal/portal/login";

// curl 写回调 —— 将响应数据追加到 string
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// 获取本机当前网络 IP (UDP 连接不实际发包, 仅取路由出接口地址)
static std::string GetLocalIp()
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return "";

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

    std::string ip;
    if (connect(sock, (sockaddr*)&remote, sizeof(remote)) == 0)
    {
        sockaddr_in local{};
        int len = sizeof(local);
        if (getsockname(sock, (sockaddr*)&local, &len) == 0)
        {
            char buf[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
            if (std::string(buf) != "0.0.0.0")
                ip = buf;
        }
    }
    closesocket(sock);
    return ip;
}

// UTF-8 → 宽字符, 供 MessageBoxW 使用
static std::wstring Utf8ToWide(const std::string& text)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    std::wstring wmsg(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wmsg[0], wlen);
    return wmsg;
}

// 宿舍区 ePortal 认证, 成功返回 true, 失败填充 message (不弹窗)
static bool LoginDormitory(const std::string& Account, const std::string& Password, std::string& message)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        message = "curl 初始化失败";
        return false;
    }

    std::string FullUrl = LOGIN_URL_BASE
        + "?user_account=%2C0%2C" + Account
        + "&user_password=" + Password;

    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_URL, FullUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);

    CURLcode res = curl_easy_perform(curl);
    bool success = false;

    if (res != CURLE_OK)
    {
        message = "宿舍区请求失败: " + std::string(curl_easy_strerror(res));
    }
    else
    {
        // 剥离 jsonpReturn( ... ) 外层, 取内层纯 JSON
        size_t start = responseBody.find('(');
        size_t end   = responseBody.rfind(')');
        if (start != std::string::npos && end != std::string::npos && end > start)
        {
            try {
                nlohmann::json resp = nlohmann::json::parse(responseBody.substr(start + 1, end - start - 1));
                int result = resp.value("result", 1);
                std::string msg = resp.value("msg", "");
                success = (result == 0);
                message = msg.empty() ? (success ? "宿舍区认证成功" : "宿舍区认证失败") : msg;
            }
            catch (const std::exception&)
            {
                message = "宿舍区响应 JSON 解析失败";
            }
        }
        else
        {
            message = responseBody;
        }
    }
    curl_easy_cleanup(curl);
    return success;
}

// 认证入口: 教学/办公区(SRun)优先, 失败自动回退宿舍区(ePortal)
bool Login(std::string Account, std::string Password)
{
    curl_global_init(CURL_GLOBAL_ALL);

    std::string srunMsg, dormMsg, method;
    bool ok = SrunLogin(Account, Password, srunMsg);
    if (ok)
    {
        method = "SRun(教学区)";
    }
    else
    {
        ok = LoginDormitory(Account, Password, dormMsg);
        method = ok ? "ePortal(宿舍区)" : "SRun→ePortal";
    }

    // 认证日志: 成功记设备IP, 失败记失败原因
    std::string logReason;
    if (!ok)
        logReason = "教学区: " + (srunMsg.empty() ? std::string("无") : srunMsg)
            + "; 宿舍区: " + (dormMsg.empty() ? std::string("无") : dormMsg);
    WriteAuthLog(method, ok, ok ? GetLocalIp() : "", logReason);

    if (FirstBoot)
    {
        if (ok)
            MessageBoxW(NULL, Utf8ToWide(srunMsg.empty() ? dormMsg : srunMsg).c_str(), L"提示", MB_OK);
        else
            MessageBoxW(NULL, Utf8ToWide(
                "教学区认证失败: " + (srunMsg.empty() ? std::string("无") : srunMsg)
                + "\n宿舍区认证失败: " + (dormMsg.empty() ? std::string("无") : dormMsg)
            ).c_str(), L"提示", MB_OK);
    }

    curl_global_cleanup();
    return ok;
}

// TCP connect 测试目标可达性, 非阻塞 + select 控制超时
bool NetworkCheck(const char* ip, int port, int timeoutMs)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    connect(sock, (sockaddr*)&addr, sizeof(addr));

    fd_set fd;
    FD_ZERO(&fd);
    FD_SET(sock, &fd);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};

    bool ok = select(0, nullptr, &fd, nullptr, &tv) > 0;

    closesocket(sock);
    WSACleanup();
    return ok;
}
