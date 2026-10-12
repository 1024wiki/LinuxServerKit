#ifndef RTSP_REQUEST_H_
#define RTSP_REQUEST_H_

#include "RtspCommon.h"

// RTSP 支持的方法枚举
enum class RtspMethod {
    NONE = 0,
    OPTIONS,
    DESCRIBE,
    SETUP,
    PLAY,
    PAUSE,
    TEARDOWN,
    UNSUPPORTED
};

// 解析后的 RTSP 请求结构体
struct RtspRequest {
    RtspMethod method = RtspMethod::NONE;
    std::string method_str;
    std::string url;
    std::string version;
    std::map<std::string, std::string> headers; // 小写化 key 以便快速匹配
    std::string body;

    int cseq = 0;
    std::string session_id;
    int client_rtp_port = 0;
    int client_rtcp_port = 0;
    double range_start = 0.0;
    bool has_range = false;
    int track_id = -1; // -1 表示整流（如 DESCRIBE/PLAY），0=video, 1=audio

    std::string GetHeader(const std::string& key) const {
        std::string lower_key = key;
        for (char& c : lower_key) c = ::tolower(c);
        auto it = headers.find(lower_key);
        return it != headers.end() ? it->second : "";
    }
};

class RtspParser {
public:
    // 从文本中尝试解析一个完整的 RTSP 请求（以 \r\n\r\n 结尾）
    // parsed_bytes 返回消耗的字节数（包含尾部 \r\n\r\n）
    static bool Parse(const char* data, size_t len, RtspRequest& req, size_t& parsed_bytes);
};

#endif
