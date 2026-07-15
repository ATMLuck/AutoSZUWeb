#pragma once
#include <string>

void SetAutoStart();

// DPAPI 加解密 —— 明文 ↔ Base64 密文
std::string EncryptStr(const std::string& plaintext);
std::string DecryptStr(const std::string& ciphertext);