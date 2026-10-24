#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Core 库内部共享模块（非公共 API，勿移入 include/）：
// Base64 编解码、AES-256-GCM 加解密与凭据错误状态。
// 供 src/<platform>/sys.cpp 的平台凭据实现复用，加密实现保持单份。
namespace CredentialCrypto
{
    constexpr size_t kAesKeySize = 32;

    std::string Base64Encode(const unsigned char* data, size_t length);
    std::vector<unsigned char> Base64Decode(const std::string& encoded);

    // 密文格式 "v1:" + Base64(iv || ciphertext || tag)；失败返回空串并记录错误原因。
    std::string EncryptAesGcm(const std::vector<unsigned char>& key,
                              const std::string& plaintext);
    std::string DecryptAesGcm(const std::vector<unsigned char>& key,
                              const std::string& encoded);

    // 凭据错误状态（thread_local）：失败时 SetError，成功路径 ClearError。
    void SetError(const std::string& message);
    void ClearError();
    const std::string& GetError();
}
