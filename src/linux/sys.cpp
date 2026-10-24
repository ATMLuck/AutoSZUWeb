#include "sys.h"

#include "credential_crypto.h"

#include <array>
#include <string>
#include <vector>

namespace
{
    // Linux 仅用于 CI/开发期逻辑测试，不用于发布或真实凭据保护。
    std::vector<unsigned char> PlatformEncryptionKey()
    {
        constexpr std::array<unsigned char, CredentialCrypto::kAesKeySize> kTestKey = {
            0x41,0x75,0x74,0x6f,0x53,0x5a,0x55,0x57,
            0x65,0x62,0x2d,0x4c,0x69,0x6e,0x75,0x78,
            0x2d,0x54,0x65,0x73,0x74,0x2d,0x4b,0x65,
            0x79,0x2d,0x30,0x30,0x30,0x30,0x30,0x31
        };
        CredentialCrypto::ClearError();
        return {kTestKey.begin(), kTestKey.end()};
    }
}

void SetAutoStart()
{
    // Linux 分支仅为开发期编译与测试桩。
}

std::vector<std::string> GetUtf8Arguments(int argc, char* argv[])
{
    // Linux 的 argv 本身就是 UTF-8 编码。
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
        arguments.push_back(argv[i] ? argv[i] : "");
    return arguments;
}

bool PrepareCliConsole()
{
    return true;
}

std::string EncryptStr(const std::string& plaintext)
{
    std::vector<unsigned char> key = PlatformEncryptionKey();
    if (key.size() != CredentialCrypto::kAesKeySize)
        return {};
    return CredentialCrypto::EncryptAesGcm(key, plaintext);
}

std::string DecryptStr(const std::string& ciphertext)
{
    std::vector<unsigned char> key = PlatformEncryptionKey();
    if (key.size() != CredentialCrypto::kAesKeySize)
        return {};
    return CredentialCrypto::DecryptAesGcm(key, ciphertext);
}
