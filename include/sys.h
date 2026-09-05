#pragma once
#include <string>

void SetAutoStart();

// DPAPI 加解密 —— 明文 ↔ Base64 密文
std::string EncryptStr(const std::string& plaintext);
std::string DecryptStr(const std::string& ciphertext);

/*
    认证日志: 追加一行到 %APPDATA%\AutoSZUWeb\logs\auth_YYYY-MM-DD.log
    记录时间/认证方式/结果; 成功记设备IP, 失败记失败原因
*/
void WriteAuthLog(const std::string& method, bool success,
                  const std::string& deviceIp, const std::string& message);