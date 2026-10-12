#include "RtspRequest.h"
#include <cstring>
#include <sstream>
#include <algorithm>

bool RtspParser::Parse(const char* data, size_t len, RtspRequest& req, size_t& parsed_bytes) {
    if (!data || len == 0) return false;

    // 寻找请求头结束标志 \r\n\r\n
    const char* end_ptr = strstr(data, "\r\n\r\n");
    if (!end_ptr) {
        return false; // 半包，等待更多数据
    }

    size_t header_len = (end_ptr - data) + 4;
    std::string header_text(data, header_len - 4);

    std::istringstream stream(header_text);
    std::string line;

    // 1. 请求行 (Request Line): METHOD URL VERSION
    if (!std::getline(stream, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::istringstream line_stream(line);
    line_stream >> req.method_str >> req.url >> req.version;

    if (req.method_str == "OPTIONS")  req.method = RtspMethod::OPTIONS;
    else if (req.method_str == "DESCRIBE") req.method = RtspMethod::DESCRIBE;
    else if (req.method_str == "SETUP")    req.method = RtspMethod::SETUP;
    else if (req.method_str == "PLAY")     req.method = RtspMethod::PLAY;
    else if (req.method_str == "PAUSE")    req.method = RtspMethod::PAUSE;
    else if (req.method_str == "TEARDOWN") req.method = RtspMethod::TEARDOWN;
    else req.method = RtspMethod::UNSUPPORTED;

    // 解析 trackID
    size_t track_pos = req.url.find("trackID=");
    if (track_pos != std::string::npos) {
        req.track_id = std::atoi(req.url.c_str() + track_pos + 8);
    } else {
        req.track_id = -1;
    }

    // 2. 请求头 (Headers)
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);

            // 去除首尾空格
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();

            std::string lower_key = key;
            for (char& c : lower_key) c = ::tolower(c);
            req.headers[lower_key] = val;
        }
    }

    // 常用字段快速提取
    std::string cseq_str = req.GetHeader("CSeq");
    if (!cseq_str.empty()) req.cseq = std::atoi(cseq_str.c_str());

    std::string session_str = req.GetHeader("Session");
    if (!session_str.empty()) {
        size_t semi = session_str.find(';');
        req.session_id = (semi != std::string::npos) ? session_str.substr(0, semi) : session_str;
    }

    // Transport 提取 client_port=x-y
    std::string transport = req.GetHeader("Transport");
    if (!transport.empty()) {
        size_t cp = transport.find("client_port=");
        if (cp != std::string::npos) {
            size_t dash = transport.find('-', cp + 12);
            if (dash != std::string::npos) {
                req.client_rtp_port = std::atoi(transport.c_str() + cp + 12);
                req.client_rtcp_port = std::atoi(transport.c_str() + dash + 1);
            }
        }
    }

    // Range 提取 npt=x-
    std::string range = req.GetHeader("Range");
    if (!range.empty()) {
        req.has_range = true;
        size_t npt_pos = range.find("npt=");
        if (npt_pos != std::string::npos) {
            req.range_start = std::atof(range.c_str() + npt_pos + 4);
        }
    }

    // 检查是否有 Content-Length
    size_t content_len = 0;
    std::string cl_str = req.GetHeader("Content-Length");
    if (!cl_str.empty()) content_len = std::strtoul(cl_str.c_str(), nullptr, 10);

    if (len < header_len + content_len) {
        return false; // Body 还没收齐
    }

    if (content_len > 0) {
        req.body.assign(data + header_len, content_len);
    }

    parsed_bytes = header_len + content_len;
    return true;
}
