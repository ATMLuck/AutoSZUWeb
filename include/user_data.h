#pragma once

#include <filesystem>
#include <string>

namespace UserData
{
    // 创建首次配置模板。返回前保证内容已经刷新并关闭文件。
    bool CreateTemplate(const std::filesystem::path& path, std::string& error);

    // 读取前两行账号和密码。返回前保证输入文件已经关闭。
    bool ReadCredentials(const std::filesystem::path& path,
                         std::string& account,
                         std::string& password,
                         std::string& error);

    // 删除配置临时文件；失败时返回可展示的错误信息。
    bool RemoveFile(const std::filesystem::path& path, std::string& error);
}