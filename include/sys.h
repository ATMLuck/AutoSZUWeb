#pragma once

#include <string>

// 注册当前程序为用户登录后自动启动。
void SetAutoStart();

// 平台凭据保护：Windows 使用 DPAPI；macOS 使用 Keychain 密钥 + AES-256-GCM。
std::string EncryptStr(const std::string& plaintext);
std::string DecryptStr(const std::string& ciphertext);

// 返回最近一次凭据保护失败原因；成功操作后为空。
std::string GetCredentialError();

// 认证日志：按天追加到应用数据目录的 logs 子目录。
void WriteAuthLog(const std::string& method, bool success,
                  const std::string& deviceIp, const std::string& message);
