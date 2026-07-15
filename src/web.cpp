#include "curl/curl.h"
#include <string>
#include <iostream>
const std::string LOGIN_URL_BASE = "http://172.30.255.42:801/eportal/portal/login";
void Login(std::string Account,std::string Password)
{
    curl_global_init(CURL_GLOBAL_ALL);
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "curl初始化失败" << std::endl;
        system("pause");
        std::exit(0);
    }
    std::string FullUrl = LOGIN_URL_BASE + "?user_account=%2C0%2C" + Account + "&user_password=" + Password;
    curl_easy_setopt(curl, CURLOPT_URL, FullUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "请求失败：" << curl_easy_strerror(res) << std::endl;
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}