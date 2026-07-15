#include "curl/curl.h"
#include "nlohmann/json.hpp"
#include <windows.h>
#include <string>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include <shlobj.h>
#include <vector>
#include <filesystem>
#pragma comment(lib, "shell32.lib")
namespace fs = std::filesystem;
bool SetAutoStart(const std::wstring& appName, const std::wstring& appPath) {
    HKEY hKey;
    LPCWSTR subKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        subKey,
        0,
        KEY_SET_VALUE,
        &hKey
    );
    if (result != ERROR_SUCCESS) {
        std::wcerr << L"打开注册表失败，错误代码：" << result << std::endl;
        return false;
    }
    result = RegSetValueExW(
        hKey,
        appName.c_str(),
        0,
        REG_SZ,
        (const BYTE*)appPath.c_str(),
        (appPath.size() + 1) * sizeof(WCHAR)
    );

    RegCloseKey(hKey);
    if (result != ERROR_SUCCESS) {
        std::wcerr << L"写入注册表失败，错误代码：" << result << std::endl;
        return false;
    }
    return true;
}

fs::path GetUsersFolderPath()
{
    WCHAR widePath[MAX_PATH];
    HRESULT result = SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, widePath);
    if (FAILED(result))
    {
        return "";
    }
    int bufferSize = WideCharToMultiByte
    (
        CP_ACP,
        0,
        widePath,
        -1,
        NULL,
        0,
        NULL, NULL
    );
    if (bufferSize <= 0)
    {
        return "";
    }
    std::vector<char> multiByteBuffer(bufferSize);
    WideCharToMultiByte
    (
        CP_ACP,
        0,
        widePath,
        -1,
        multiByteBuffer.data(),
        bufferSize,
        NULL, NULL
    );
    return fs::path(multiByteBuffer.data());
}
fs::path GetDesktopPath()
{
    WCHAR widePath[MAX_PATH];
    HRESULT result = SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, widePath);
    if (FAILED(result))
    {
        return "";
    }

    int bufferSize = WideCharToMultiByte
    (
        CP_ACP,
        0,
        widePath,
        -1,
        NULL,
        0,
        NULL, NULL
    );
    if (bufferSize <= 0)
    {
        return "";
    }
    std::vector<char> multiByteBuffer(bufferSize);
    WideCharToMultiByte
    (
        CP_ACP,
        0,
        widePath,
        -1,
        multiByteBuffer.data(),
        bufferSize,
        NULL, NULL
    );
    return fs::path(multiByteBuffer.data());
}
void newuser()
{
    std::cout << "\a"; 
    if(MessageBoxW(NULL,L"请打开桌面上的userdata.txt文"
        "件，并按照文件内提示写入校园卡号和统一身份认证平台"
        "密码，并重新启动本程序。ᖰ˃̵ ֊ ˂̵ᖳ",L"提示",
        MB_OKCANCEL|MB_ICONASTERISK)==IDCANCEL)
    {
        std::exit(0);
    }
    fs::path filename = "userdata.txt";
    fs::path DesktopPath = GetDesktopPath();
    fs::path file_path =  DesktopPath / filename;
    std::ofstream outFile(file_path);
    if (!outFile.is_open())
    {
        std::cerr << "无法创建/打开文件进行写入！" << std::endl;
        std::exit(1);
    }
    outFile << "(请将此行替换为校园卡号)\n";
    outFile << "(请将此行替换为统一身份认证平台密码,替换完记得Ctrl+s保存ᖰ˃̵ ֊ ˂̵ᖳ)";
    outFile.close();
    std::exit(0);
}
void mainwork()
{
    curl_global_init(CURL_GLOBAL_ALL);
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "curl初始化失败" << std::endl;
        system("pause");
        std::exit(0);
    }
    nlohmann::json Logindata;
    fs::path Loginfilename = "autoWEB.json";
    fs::path LoginbasePath = GetUsersFolderPath();
    fs::path LoginPath =  LoginbasePath / Loginfilename;
    
    std::ifstream LoginFile(LoginPath);
    if (!LoginFile.is_open())
    {
        std::cerr << "无法读取指定路径的文件：" << LoginPath << std::endl;
        system("pause");
        std::exit(0);
    }
    LoginFile >> Logindata;
    LoginFile.close();

    const std::string DRCOM_CARD_ID = Logindata["Account"].get<std::string>();
    const std::string DRCOM_PASSWORD = Logindata["Password"].get<std::string>();
    const std::string LOGIN_URL_BASE = "http://172.30.255.42:801/eportal/portal/login";
    std::string fullUrl = LOGIN_URL_BASE + "?user_account=%2C0%2C" + DRCOM_CARD_ID + "&user_password=" + DRCOM_PASSWORD;


    curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);


    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "请求失败：" << curl_easy_strerror(res) << std::endl;
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    system("pause");
    std::exit(0);
}
int main() {
    SetConsoleOutputCP(65001);
    fs::path first_filename = "autoWEB.json";
    fs::path first_AppDataPath = GetUsersFolderPath();
    fs::path first_file_path =  first_AppDataPath / first_filename;
    if (fs::exists(first_file_path) && fs::is_regular_file(first_file_path))
    {
        mainwork();
    }
    else
    {
        fs::path in_filename = "userdata.txt";
        fs::path in_DesktopPath = GetDesktopPath();
        fs::path in_file_path =  in_DesktopPath / in_filename;
        if (fs::exists(in_file_path) && fs::is_regular_file(in_file_path))
        {
            std::ifstream inFile(in_file_path);
            if (!inFile.is_open())
            {
                std::cerr << "无法读取指定路径的文件：" << in_file_path << std::endl;
                return 1;
            }
            std::string line;
            fs::path out_filename = "autoWEB.json";
            fs::path out_AppdataPath = GetUsersFolderPath();
            fs::path out_file_path =  out_AppdataPath / out_filename;
            std::ofstream outFile(out_file_path);
            if (!outFile.is_open())
            {
                std::cerr << "无法创建/打开文件进行写入！" << std::endl;
                return 1;
            }
            nlohmann::json Userdata;
            int i = 0;
            while (std::getline(inFile, line))
            {
                if(i == 0) Userdata["Account"] = line;
                if(i == 1) Userdata["Password"] = line;
                i++;
            }
            outFile << Userdata;
            inFile.close();
            outFile.close();
            WCHAR appPath[MAX_PATH];
            GetModuleFileNameW(NULL, appPath, MAX_PATH);
            std::wstring appName = L"AutoSZUWeb";
            SetAutoStart(appName, appPath);
            std::remove(in_file_path.string().c_str());
            mainwork();
        }
        else
        {
            newuser();
        }
    }
    return 0;
}