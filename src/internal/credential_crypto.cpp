#include "credential_crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace
{
    constexpr size_t kGcmIvSize = 12;
    constexpr size_t kGcmTagSize = 16;
    thread_local std::string gCredentialError;
}

namespace CredentialCrypto
{
    void SetError(const std::string& message)
    {
        gCredentialError = message;
    }

    void ClearError()
    {
        gCredentialError.clear();
    }

    const std::string& GetError()
    {
        return gCredentialError;
    }

    std::string Base64Encode(const unsigned char* data, size_t length)
    {
        if (length == 0)
            return {};
        std::vector<unsigned char> output(4 * ((length + 2) / 3) + 1);
        const int outputLength = EVP_EncodeBlock(output.data(), data,
            static_cast<int>(length));
        return outputLength > 0
            ? std::string(reinterpret_cast<char*>(output.data()), outputLength)
            : std::string{};
    }

    std::vector<unsigned char> Base64Decode(const std::string& encoded)
    {
        if (encoded.empty())
            return {};
        std::vector<unsigned char> output((encoded.size() * 3) / 4 + 3);
        int length = EVP_DecodeBlock(output.data(),
            reinterpret_cast<const unsigned char*>(encoded.data()),
            static_cast<int>(encoded.size()));
        if (length < 0)
            return {};
        if (!encoded.empty() && encoded.back() == '=') --length;
        if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=') --length;
        output.resize(static_cast<size_t>(length));
        return output;
    }

    std::string EncryptAesGcm(const std::vector<unsigned char>& key,
                              const std::string& plaintext)
    {
        if (key.size() != kAesKeySize)
        {
            SetError("凭据密钥长度无效");
            return {};
        }

        std::vector<unsigned char> iv(kGcmIvSize);
        if (RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1)
        {
            SetError("生成 AES-GCM 随机 IV 失败");
            return {};
        }

        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (!context)
        {
            SetError("创建 AES-GCM 加密上下文失败");
            return {};
        }

        std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
        int length = 0;
        int totalLength = 0;
        bool ok =
            EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
                static_cast<int>(iv.size()), nullptr) == 1 &&
            EVP_EncryptInit_ex(context, nullptr, nullptr, key.data(), iv.data()) == 1 &&
            EVP_EncryptUpdate(context, ciphertext.data(), &length,
                reinterpret_cast<const unsigned char*>(plaintext.data()),
                static_cast<int>(plaintext.size())) == 1;
        totalLength = length;
        if (ok)
            ok = EVP_EncryptFinal_ex(context, ciphertext.data() + totalLength, &length) == 1;
        totalLength += length;

        std::vector<unsigned char> tag(kGcmTagSize);
        if (ok)
            ok = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG,
                static_cast<int>(tag.size()), tag.data()) == 1;
        EVP_CIPHER_CTX_free(context);
        if (!ok)
        {
            SetError("AES-256-GCM 加密失败");
            return {};
        }

        ciphertext.resize(static_cast<size_t>(totalLength));
        std::vector<unsigned char> payload;
        payload.reserve(iv.size() + ciphertext.size() + tag.size());
        payload.insert(payload.end(), iv.begin(), iv.end());
        payload.insert(payload.end(), ciphertext.begin(), ciphertext.end());
        payload.insert(payload.end(), tag.begin(), tag.end());
        const std::string encoded = Base64Encode(payload.data(), payload.size());
        if (encoded.empty())
        {
            SetError("AES-GCM 密文 Base64 编码失败");
            return {};
        }
        ClearError();
        return "v1:" + encoded;
    }

    std::string DecryptAesGcm(const std::vector<unsigned char>& key,
                              const std::string& encoded)
    {
        if (key.size() != kAesKeySize)
        {
            SetError("凭据密钥长度无效");
            return {};
        }
        if (encoded.rfind("v1:", 0) != 0)
        {
            SetError("凭据密文版本不受支持");
            return {};
        }
        std::vector<unsigned char> payload = Base64Decode(encoded.substr(3));
        if (payload.size() < kGcmIvSize + kGcmTagSize)
        {
            SetError("凭据密文格式无效");
            return {};
        }

        const unsigned char* iv = payload.data();
        const size_t ciphertextLength = payload.size() - kGcmIvSize - kGcmTagSize;
        const unsigned char* ciphertext = payload.data() + kGcmIvSize;
        unsigned char* tag = payload.data() + kGcmIvSize + ciphertextLength;

        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (!context)
        {
            SetError("创建 AES-GCM 解密上下文失败");
            return {};
        }
        std::vector<unsigned char> plaintext(ciphertextLength + EVP_MAX_BLOCK_LENGTH);
        int length = 0;
        int totalLength = 0;
        bool ok =
            EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
                static_cast<int>(kGcmIvSize), nullptr) == 1 &&
            EVP_DecryptInit_ex(context, nullptr, nullptr, key.data(), iv) == 1 &&
            EVP_DecryptUpdate(context, plaintext.data(), &length, ciphertext,
                static_cast<int>(ciphertextLength)) == 1;
        totalLength = length;
        if (ok)
            ok = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG,
                static_cast<int>(kGcmTagSize), tag) == 1;
        if (ok)
            ok = EVP_DecryptFinal_ex(context, plaintext.data() + totalLength, &length) == 1;
        totalLength += length;
        EVP_CIPHER_CTX_free(context);
        if (!ok)
        {
            SetError("AES-GCM 认证失败，凭据可能已损坏或被篡改");
            return {};
        }
        ClearError();
        return std::string(reinterpret_cast<char*>(plaintext.data()), totalLength);
    }
}
