#include "file_path.h"
#include "web.h"
#include "sys.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <string>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include <shlobj.h>
#include <vector>
#include <filesystem>
#include <curl/curl.h>
namespace fs = std::filesystem;
const std::string ConfigFile = "autoWEB.json";

void mainwork()
{
    nlohmann::json Logindata;
    fs::path LoginPath =  GetUsersFolderPath() / ConfigFile;
    
    std::ifstream LoginFile(LoginPath);
    LoginFile >> Logindata;
    LoginFile.close();

    std::string Account = Logindata["Account"].get<std::string>();
    std::string Password = Logindata["Password"].get<std::string>();
    Login(Account,Password);
    std::exit(0);
}
void newuser()
{
    fs::path in_file_path =  GetDesktopPath() / "userdata.txt";
    if (fs::exists(in_file_path) && fs::is_regular_file(in_file_path))
    {
        std::ifstream inFile(in_file_path);
        if (!inFile.is_open())
        {
            MessageBoxW(NULL, L"无法读取指定路径的文件：", L"提示", MB_OK);
            std::exit(1);
        }
        std::string line;
        fs::path out_file_path =  GetUsersFolderPath() / "autoWEB.json";
        std::ofstream outFile(out_file_path);
        if (!outFile.is_open())
        {
            MessageBoxW(NULL, L"无法创建/打开文件进行写入！", L"提示", MB_OK);
            std::exit(1);;
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
        SetAutoStart();
        std::remove(in_file_path.string().c_str());
        mainwork();
    }
    else
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
            MessageBoxW(NULL, L"无法创建/打开文件进行写入！", L"提示", MB_OK);
            std::exit(1);
        }
        outFile << "(请将此行替换为校园卡号)\n";
        outFile << "(请将此行替换为统一身份认证平台密码,替换完记得Ctrl+s保存ᖰ˃̵ ֊ ˂̵ᖳ)";
        outFile.close();
        std::exit(0);
    }
}
int main() {
    SetConsoleOutputCP(65001);
    fs::path first_file_path =  GetUsersFolderPath()/ ConfigFile;
    if (fs::exists(first_file_path) && fs::is_regular_file(first_file_path))
    {
        mainwork();
    }
    else
    {
        newuser();
    }
    return 0;
}