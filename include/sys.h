#pragma once

#include <filesystem>
#include <string>
#include <vector>

// 注册当前程序为用户登录后自动启动。
void SetAutoStart();

// 移除自启动注册（Windows 的 Run 注册表值 / macOS 的 LaunchAgent plist）。
void RemoveAutoStart();

// 删除平台凭据密钥（macOS 钥匙串条目）。Windows DPAPI 密文保存在配置文件
// 内、Linux 测试桩使用内存密钥，均无独立密钥，直接返回成功。
bool RemoveCredentialKey(std::string& error);

// 卸载时删除平台专属痕迹：Windows 处理遗留配置与被占用的 exe（延迟删除），
// macOS 卸载 LaunchAgent 任务并删除应用包本体。leftovers 为共享数据删除阶段
// 未能删除的文件，平台处理成功的条目会从中移除。
bool RemovePlatformResidue(std::vector<std::filesystem::path>& leftovers,
                           std::string& error);

// 以 UTF-8 返回命令行参数（不含程序名）。Windows 下 main 的 argv 使用 ANSI
// 代码页，中文参数会乱码，因此改用宽字符 API 获取并转换；其余平台直接透传。
std::vector<std::string> GetUtf8Arguments(int argc, char* argv[]);

// CLI 模式下准备控制台输出，返回标准输出是否可用。Windows GUI 子系统程序需要
// 附加父进程控制台或确认输出已被重定向；其余平台始终可用。
bool PrepareCliConsole();

// 平台凭据保护：Windows 使用 DPAPI；macOS 使用 Keychain 密钥 + AES-256-GCM。
std::string EncryptStr(const std::string& plaintext);
std::string DecryptStr(const std::string& ciphertext);

// 返回最近一次凭据保护失败原因；成功操作后为空。
std::string GetCredentialError();

// 认证日志：按天追加到应用数据目录的 logs 子目录。
void WriteAuthLog(const std::string& method, bool success,
                  const std::string& deviceIp, const std::string& message);
