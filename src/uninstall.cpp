#include "uninstall.h"

#include "file_path.h"
#include "sys.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    void RemoveEntryBestEffort(const fs::path& path,
                               std::vector<fs::path>& leftovers)
    {
        std::error_code error;
        fs::remove_all(path, error);
        if (error || fs::exists(path, error))
            leftovers.push_back(path);
    }
}

namespace Uninstall
{
    void RemoveSharedData(std::vector<fs::path>& leftovers)
    {
        // 数据目录：setting.json、logs/、实例锁文件以及可能残留的自启动副本。
        // 逐项删除以便把被占用的文件（如正在运行的 exe 副本）单独记入
        // leftovers，交由平台善后。
        const fs::path dataRoot = GetUsersFolderPath();
        if (!dataRoot.empty())
        {
            const fs::path dataDirectory = dataRoot / "AutoSZUWeb";
            std::error_code error;
            if (fs::exists(dataDirectory, error))
            {
                std::error_code iterationError;
                for (const auto& entry :
                     fs::directory_iterator(dataDirectory, iterationError))
                    RemoveEntryBestEffort(entry.path(), leftovers);
                if (iterationError)
                    leftovers.push_back(dataDirectory);
                else
                    RemoveEntryBestEffort(dataDirectory, leftovers);
            }
        }

        // 桌面上待首次配置的 userdata.txt（含明文凭据）。
        const fs::path desktop = GetDesktopPath();
        if (!desktop.empty())
            RemoveEntryBestEffort(desktop / "userdata.txt", leftovers);
    }

    bool RemoveAll(std::string& error)
    {
        // 先摘自启动，避免后续步骤失败后仍保持开机自启。
        RemoveAutoStart();

        std::vector<fs::path> leftovers;
        RemoveSharedData(leftovers);

        if (!RemovePlatformResidue(leftovers, error))
            return false;

        if (!leftovers.empty())
        {
            std::string remaining;
            for (const fs::path& path : leftovers)
                remaining += (remaining.empty() ? "" : "\n") + path.u8string();
            error = "以下内容未能删除（可能正被占用），"
                "请结束 AutoSZUWeb 进程或重启后手动删除:\n" + remaining;
            return false;
        }
        return true;
    }
}
