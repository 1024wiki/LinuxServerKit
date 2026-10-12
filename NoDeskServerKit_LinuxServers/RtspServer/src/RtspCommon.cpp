#include "RtspCommon.h"
#include <sstream>
#include <iomanip>

namespace rtsp_base64 {

static const char TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode(const uint8_t* data, size_t len)
{
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < len) {
        uint32_t v = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
        out += TABLE[(v >> 18) & 0x3F];
        out += TABLE[(v >> 12) & 0x3F];
        out += TABLE[(v >>  6) & 0x3F];
        out += TABLE[ v        & 0x3F];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t v = data[i] << 16;
        out += TABLE[(v >> 18) & 0x3F];
        out += TABLE[(v >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        uint32_t v = (data[i] << 16) | (data[i+1] << 8);
        out += TABLE[(v >> 18) & 0x3F];
        out += TABLE[(v >> 12) & 0x3F];
        out += TABLE[(v >>  6) & 0x3F];
        out += '=';
    }
    return out;
}

} // namespace rtsp_base64

namespace rtsp_url {

std::string decode(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int high = str[i+1];
            int low = str[i+2];
            auto hex2int = [](int c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hex2int(high);
            int l = hex2int(low);
            if (h >= 0 && l >= 0) {
                out += static_cast<char>((h << 4) | l);
                i += 2;
                continue;
            }
        } else if (str[i] == '+') {
            out += ' ';
            continue;
        }
        out += str[i];
    }
    return out;
}

std::string extract_file_path(const std::string& url) {
    // 客户端构造规则：rtsp://<ip>:<port>/stream/<文件绝对路径>
    // 典型示例：rtsp://192.168.1.100:8554/stream//home/zhangsan/dataset/run1.mp4
    // 或带 track: rtsp://192.168.1.100:8554/stream//home/zhangsan/dataset/run1.mp4/trackID=0
    std::string decoded = decode(url);
    const std::string stream_prefix = "/stream";
    size_t pos = decoded.find(stream_prefix);
    if (pos == std::string::npos) {
        return "";
    }
    std::string path_part = decoded.substr(pos + stream_prefix.size());
    // 如果尾部有 /trackID=X，截断它
    size_t track_pos = path_part.find("/trackID=");
    if (track_pos != std::string::npos) {
        path_part = path_part.substr(0, track_pos);
    }

    // 容忍双斜杠：如果开头是 "//"，转换为 "/"
    while (path_part.size() >= 2 && path_part[0] == '/' && path_part[1] == '/') {
        path_part = path_part.substr(1);
    }
    return path_part;
}

} // namespace rtsp_url
