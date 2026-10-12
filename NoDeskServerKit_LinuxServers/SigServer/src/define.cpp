#include "define.h"

// ==================== Base64 ====================
namespace base64 {

static const char TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode(const char* data, size_t len)
{
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < len) {
        uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i+1] << 8) | (uint8_t)data[i+2];
        out += TABLE[(v >> 18) & 0x3F];
        out += TABLE[(v >> 12) & 0x3F];
        out += TABLE[(v >>  6) & 0x3F];
        out += TABLE[ v        & 0x3F];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t v = (uint8_t)data[i] << 16;
        out += TABLE[(v >> 18) & 0x3F];
        out += TABLE[(v >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i+1] << 8);
        out += TABLE[(v >> 18) & 0x3F];
        out += TABLE[(v >> 12) & 0x3F];
        out += TABLE[(v >>  6) & 0x3F];
        out += '=';
    }
    return out;
}

static int decode_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string decode(const char* data, size_t len)
{
    std::string out;
    out.reserve(len / 4 * 3 + 3);
    int val = 0, valb = -8;
    for (size_t i = 0; i < len; ++i) {
        char c = data[i];
        if (c == '=' || c == '\n' || c == '\r') continue;
        int d = decode_val(c);
        if (d < 0) continue;   // 跳过非法字符
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out += (char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return out;
}
} // namespace base64
