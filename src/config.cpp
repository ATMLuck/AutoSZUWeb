#include "config.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
    bool ReadJsonFile(const fs::path& configPath, nlohmann::json& data)
    {
        std::error_code error;
        if (!fs::is_regular_file(configPath, error))
            return false;
        std::ifstream input(configPath);
        if (!input)
            return false;
        try
        {
            input >> data;
        }
        catch (const std::exception&)
        {
            return false;
        }
        return data.is_object();
    }
}

namespace Config
{
    bool ReadAutoAuthEnabled(const fs::path& configPath)
    {
        nlohmann::json data;
        if (!ReadJsonFile(configPath, data))
            return true;
        if (!data.contains("AutoAuth") || !data["AutoAuth"].is_boolean())
            return true;
        return data["AutoAuth"].get<bool>();
    }

    bool WriteAutoAuthEnabled(const fs::path& configPath, bool enabled,
                              std::string& error)
    {
        std::error_code fileError;
        if (!fs::is_regular_file(configPath, fileError))
        {
            error = "配置文件不存在，请先完成首次配置";
            return false;
        }

        nlohmann::json data;
        if (!ReadJsonFile(configPath, data))
        {
            error = "配置文件格式错误，无法修改";
            return false;
        }
        data["AutoAuth"] = enabled;

        fs::path temporaryPath = configPath;
        temporaryPath += ".tmp";
        {
            std::ofstream output(temporaryPath, std::ios::trunc);
            if (!output)
            {
                error = "无法创建临时配置文件";
                return false;
            }
            output << data.dump(2);
            if (!output)
            {
                error = "写入配置文件失败";
                return false;
            }
        }

        std::error_code renameError;
        fs::rename(temporaryPath, configPath, renameError);
        if (renameError)
        {
            fs::remove(configPath, renameError);
            renameError.clear();
            fs::rename(temporaryPath, configPath, renameError);
        }
        if (renameError)
        {
            error = "保存配置文件失败：" + renameError.message();
            return false;
        }
        return true;
    }
}
