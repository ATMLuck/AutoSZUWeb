#include "srun.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <string>
#include <vector>
#include <cstring>
#include <cctype>

// SRun (深澜) 教学/办公区认证协议实现, 移植自 Python 版 srun.py。
// 关键步骤: get_challenge → 用 challenge 构造加密载荷 → srun_portal 登录。
// 全部请求复用同一个 curl 句柄并启用 Cookie 引擎, 与参考实现同 session 行为一致。
namespace
{
    const std::string SRUN_BASE_URL = "https://net.szu.edu.cn";
    const char* SRUN_ALPHA = "LVoJPiCN2R8G90yg+hmFHuacZ1OWMnrsSTXkYpUq/3dlbfKwv6xztjI7DeBE45QA";
    const char* STANDARD_ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint32_t UINT32_MASK = 0xFFFFFFFFu;

    // curl 写回调 —— 将响应数据追加到 string
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    // 字节 → 十六进制小写字符串
    static std::string ToHex(const unsigned char* data, size_t len)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(len * 2);
        for (size_t i = 0; i < len; ++i)
        {
            out += digits[data[i] >> 4];
            out += digits[data[i] & 0x0F];
        }
        return out;
    }

    // 每 4 字节小端拼为一个 uint32, 可选末尾追加原始字节长度
    static std::vector<uint32_t> Words(const std::string& content, bool include_length)
    {
        std::vector<uint32_t> values;
        const unsigned char* data = (const unsigned char*)content.data();
        size_t len = content.size();
        for (size_t index = 0; index < len; index += 4)
        {
            uint32_t v = 0;
            for (int offset = 0; offset < 4; ++offset)
            {
                size_t i = index + offset;
                v |= (uint32_t)(i < len ? data[i] : 0) << (offset * 8);
            }
            values.push_back(v);
        }
        if (include_length)
            values.push_back((uint32_t)len);
        return values;
    }

    // XXTEA 兼容加密, 与 Python xencode 逐字节一致
    static std::string XEncode(const std::string& content, const std::string& key)
    {
        if (content.empty())
            return "";

        std::vector<uint32_t> values = Words(content, true);
        std::vector<uint32_t> key_values = Words(key, false);
        while (key_values.size() < 4)
            key_values.push_back(0);

        int n = (int)values.size() - 1;
        uint32_t z = values[n];
        uint32_t y = values[0];
        const uint32_t delta = 0x9E3779B9;
        uint32_t total = 0;
        int rounds = 6 + 52 / (n + 1);

        while (rounds > 0)
        {
            total = (total + delta) & UINT32_MASK;
            uint32_t e = (total >> 2) & 3;
            for (int p = 0; p < n; ++p)
            {
                y = values[p + 1];
                uint32_t mixed = ((z >> 5) ^ ((y << 2) & UINT32_MASK)) & UINT32_MASK;
                mixed = (mixed + (((y >> 3) ^ ((z << 4) & UINT32_MASK)) ^ (total ^ y))) & UINT32_MASK;
                mixed = (mixed + (key_values[(p & 3) ^ e] ^ z)) & UINT32_MASK;
                values[p] = (values[p] + mixed) & UINT32_MASK;
                z = values[p];
            }
            y = values[0];
            uint32_t mixed = ((z >> 5) ^ ((y << 2) & UINT32_MASK)) & UINT32_MASK;
            mixed = (mixed + (((y >> 3) ^ ((z << 4) & UINT32_MASK)) ^ (total ^ y))) & UINT32_MASK;
            mixed = (mixed + (key_values[(n & 3) ^ e] ^ z)) & UINT32_MASK;
            values[n] = (values[n] + mixed) & UINT32_MASK;
            z = values[n];
            rounds--;
        }

        std::string output;
        output.reserve(values.size() * 4);
        for (uint32_t value : values)
        {
            output.push_back((char)(value & 0xFF));
            output.push_back((char)((value >> 8) & 0xFF));
            output.push_back((char)((value >> 16) & 0xFF));
            output.push_back((char)((value >> 24) & 0xFF));
        }
        return output;
    }

    // 标准 Base64 → SRun 自定义字母表
    static std::string SrunBase64(const std::string& in)
    {
        if (in.empty())
            return "";
        std::vector<unsigned char> buf(4 * ((in.size() + 2) / 3) + 1);
        int outlen = EVP_EncodeBlock(buf.data(), (const unsigned char*)in.data(), (int)in.size());
        std::string encoded((const char*)buf.data(), (size_t)outlen);
        for (char& c : encoded)
        {
            const char* p = std::strchr(STANDARD_ALPHA, c);
            if (p)
                c = SRUN_ALPHA[p - STANDARD_ALPHA];
        }
        return encoded;
    }

    // HMAC-MD5, key=challenge, msg=password, 输出十六进制
    static std::string HmacMd5Hex(const std::string& key, const std::string& data)
    {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int len = 0;
        HMAC(EVP_md5(), key.data(), (int)key.size(),
             (const unsigned char*)data.data(), data.size(), digest, &len);
        return ToHex(digest, len);
    }

    static std::string Sha1Hex(const std::string& data)
    {
        unsigned char digest[SHA_DIGEST_LENGTH];
        SHA1((const unsigned char*)data.data(), data.size(), digest);
        return ToHex(digest, SHA_DIGEST_LENGTH);
    }

    // HTTP GET (TLS 关验证, 带超时), 复用外部传入的 curl 句柄, 保持 Cookie 共享
    static bool HttpGet(CURL* curl, const std::string& url, std::string& body, std::string& err)
    {
        if (!curl)
        {
            err = "curl 初始化失败";
            return false;
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/124 Safari/537.36");
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            err = curl_easy_strerror(res);
            return false;
        }
        return true;
    }

    // URL 编码参数值
    static std::string UrlEscape(CURL* curl, const std::string& value)
    {
        char* escaped = curl_easy_escape(curl, value.c_str(), 0);
        if (!escaped)
            return value;
        std::string out(escaped);
        curl_free(escaped);
        return out;
    }

    // 解析 jsonpReturn( {...} ) 外层, 返回内层 JSON
    static nlohmann::json ParseJsonp(const std::string& text)
    {
        size_t start = text.find('(');
        size_t end = text.rfind(')');
        if (start == std::string::npos || end == std::string::npos || end <= start)
            throw std::runtime_error("响应格式错误");
        return nlohmann::json::parse(text.substr(start + 1, end - start - 1));
    }

    // 从首页提取 ac_id, 失败回退 "1"
    static std::string DiscoverAcId(CURL* curl)
    {
        std::string body, err;
        if (!HttpGet(curl, SRUN_BASE_URL + "/", body, err))
            return "1";
        size_t pos = 0;
        while ((pos = body.find("ac_id=", pos)) != std::string::npos)
        {
            pos += 6;
            size_t start = pos;
            while (pos < body.size() && std::isdigit((unsigned char)body[pos]))
                ++pos;
            if (pos > start)
                return body.substr(start, pos - start);
        }
        return "1";
    }

    // 判定 srun_portal 响应是否成功: error==ok / res==ok / st==1 (含布尔 true)
    static bool PortalSuccess(const nlohmann::json& resp)
    {
        if (resp.value("error", "") == "ok" || resp.value("res", "") == "ok")
            return true;
        if (resp.contains("st"))
        {
            const auto& st = resp["st"];
            if (st.is_number_integer())
                return st.get<int>() == 1;
            if (st.is_boolean())
                return st.get<bool>();
            if (st.is_string())
                return st.get<std::string>() == "1";
        }
        return false;
    }
}

bool SrunLogin(const std::string& account, const std::string& password, std::string& message)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        message = "curl 初始化失败";
        return false;
    }
    // 启用内存 Cookie 引擎, 全流程共享会话
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");

    bool ok = false;
    message = "认证失败";
    try
    {
        std::string ac_id = DiscoverAcId(curl);

        // ① 获取 challenge 与客户端 IP
        std::string challenge_url = SRUN_BASE_URL + "/cgi-bin/get_challenge?callback=_&username="
            + UrlEscape(curl, account) + "&ip=";
        std::string body, err;
        if (!HttpGet(curl, challenge_url, body, err))
        {
            message = "获取挑战失败: " + err;
        }
        else
        {
            nlohmann::json data = ParseJsonp(body);
            std::string error = data.value("error", "");
            std::string challenge = data.value("challenge", "");
            std::string client_ip = data.value("client_ip", "");
            if (!error.empty() && error != "ok")
            {
                message = data.value("error_msg", data.value("error", "获取认证挑战失败"));
            }
            else if (challenge.empty() || client_ip.empty())
            {
                message = "认证服务器未返回 challenge 或客户端 IP";
            }
            else
            {
                // ② 构造加密载荷: info + chksum
                std::string password_md5 = HmacMd5Hex(challenge, password);
                nlohmann::ordered_json info_json = {
                    {"username", account}, {"password", password},
                    {"ip", client_ip}, {"acid", ac_id}, {"enc_ver", "srun_bx1"}
                };
                std::string info = "{SRBX1}" + SrunBase64(XEncode(info_json.dump(), challenge));
                // chksum: challenge 逐段前置 (深澜校验算法, 与 srun.py 一致)
                std::string checksum_source =
                    challenge + account + challenge + password_md5 + challenge + ac_id
                    + challenge + client_ip + challenge + "200" + challenge + "1"
                    + challenge + info;
                std::string checksum = Sha1Hex(checksum_source);

                // ③ 提交登录
                std::string portal_url = SRUN_BASE_URL + "/cgi-bin/srun_portal"
                    + "?callback=_&action=login"
                    + "&username=" + UrlEscape(curl, account)
                    + "&password=" + UrlEscape(curl, "{MD5}" + password_md5)
                    + "&os=Windows&name=Windows&double_stack=0"
                    + "&info=" + UrlEscape(curl, info)
                    + "&chksum=" + UrlEscape(curl, checksum)
                    + "&ac_id=" + UrlEscape(curl, ac_id)
                    + "&ip=" + UrlEscape(curl, client_ip)
                    + "&n=200&type=1";

                std::string portal_body, portal_err;
                if (!HttpGet(curl, portal_url, portal_body, portal_err))
                {
                    message = "登录请求失败: " + portal_err;
                }
                else
                {
                    nlohmann::json resp = ParseJsonp(portal_body);
                    ok = PortalSuccess(resp);
                    if (ok)
                        message = "教学 / 办公区认证成功";
                    else
                        message = resp.value("error_msg",
                            resp.value("error", resp.value("res", "认证失败")));
                }
            }
        }
    }
    catch (const std::exception&)
    {
        message = "教学区认证响应解析失败";
    }

    curl_easy_cleanup(curl);
    return ok;
}
