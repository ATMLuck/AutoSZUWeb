#include "curl/curl.h"
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

bool Login(std::string Account, std::string Password)
{
    curl_global_init(CURL_GLOBAL_ALL);
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        MessageBoxW(NULL,L"curl初始化失败",L"提示",MB_OK);
        return false;
    }

    std::string FullUrl = LOGIN_URL_BASE
        + "?user_account=%2C0%2C" + Account
        + "&user_password=" + Password;

    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_URL, FullUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

    CURLcode res = curl_easy_perform(curl);
    bool success = false;

    if (res != CURLE_OK)
    {
        if(FirstBoot) MessageBoxW(NULL,L"请求失败：",L"提示",MB_OK);
    }
    else
    {
        // 剥离 jsonpReturn( ... ) 外层, 取内层纯 JSON
        size_t start = responseBody.find('(');
        size_t end   = responseBody.rfind(')');
        if (start != std::string::npos && end != std::string::npos && end > start) 
        {
            std::string jsonStr = responseBody.substr(start + 1, end - start - 1);
            try {
                nlohmann::json resp = nlohmann::json::parse(jsonStr);
                int result = resp.value("result", 1);
                std::string msg = resp.value("msg", "");
                success = (result == 0);
                // 弹窗显示 msg
                int wlen = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, NULL, 0);
                std::wstring wmsg(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, &wmsg[0], wlen);
                if(FirstBoot) MessageBoxW(NULL, wmsg.c_str(), L"提示", MB_OK);
            } 
            catch (const std::exception& e) 
            {
                if(FirstBoot) MessageBoxW(NULL,L"JSON解析失败: ",L"提示",MB_OK);
            }
        } 
        else 
        {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, responseBody.c_str(), -1, NULL, 0);
            std::wstring wmsg(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, responseBody.c_str(), -1, &wmsg[0], wlen);
            if(FirstBoot) MessageBoxW(NULL, wmsg.c_str(), L"提示", MB_OK);
        }
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return success;
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
