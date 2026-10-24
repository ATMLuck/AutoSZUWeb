#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Uninstall
{
    // 删除共享数据：应用数据目录（setting.json、日志、实例锁文件）与桌面
    // userdata.txt。被占用而无法删除的条目记入 leftovers。
    void RemoveSharedData(std::vector<std::filesystem::path>& leftovers);

    // 从本机完全卸载：移除自启动注册 → 删除共享数据 → 平台善后（钥匙串
    // 密钥、自启动副本、应用包本体等）。尽力而为；仍有残留时返回 false
    // 并通过 error 说明。
    bool RemoveAll(std::string& error);
}
