#include "user_data.h"

#include <fstream>
#include <system_error>

namespace
{
    void RemoveTrailingCarriageReturn(std::string& value)
    {
        if (!value.empty() && value.back() == '\r')
            value.pop_back();
    }
}

namespace UserData
{
    bool CreateTemplate(const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        std::ofstream output(path, std::ios::trunc);
        if (!output)
        {
            error = "无法创建 userdata.txt";
            return false;
        }

        output << "(请将此行替换为校园卡号)\n"
               << "(请将此行替换为统一身份认证平台密码，替换后请保存)\n";
        output.flush();
        if (!output)
        {
            error = "写入 userdata.txt 失败";
            output.close();
            return false;
        }

        output.close();
        if (output.fail())
        {
            error = "关闭 userdata.txt 时写入失败";
            return false;
        }
        return true;
    }

    bool ReadCredentials(const std::filesystem::path& path,
                         std::string& account,
                         std::string& password,
                         std::string& error)
    {
        account.clear();
        password.clear();
        error.clear();

        {
            std::ifstream input(path);
            if (!input)
            {
                error = "无法读取 userdata.txt";
                return false;
            }

            const bool hasAccount = static_cast<bool>(std::getline(input, account));
            const bool hasPassword = static_cast<bool>(std::getline(input, password));
            if (!hasAccount || !hasPassword)
            {
                error = "userdata.txt 的前两行必须分别填写校园卡号和密码";
                return false;
            }
        }

        RemoveTrailingCarriageReturn(account);
        RemoveTrailingCarriageReturn(password);
        if (account.empty() || password.empty() ||
            account.front() == '(' || password.front() == '(')
        {
            error = "userdata.txt 的前两行必须分别填写校园卡号和密码，不能保留提示文本";
            return false;
        }
        return true;
    }

    bool RemoveFile(const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        std::error_code filesystemError;
        const bool removed = std::filesystem::remove(path, filesystemError);
        if (filesystemError)
        {
            error = "删除 userdata.txt 失败：" + filesystemError.message();
            return false;
        }
        if (!removed && std::filesystem::exists(path, filesystemError))
        {
            error = "删除 userdata.txt 失败，文件仍然存在";
            return false;
        }
        return true;
    }
}