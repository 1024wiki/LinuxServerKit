#ifndef RTSP_COMMON_H_
#define RTSP_COMMON_H_

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <netinet/in.h>

// RTSP 协议常用常量
constexpr uint16_t DEFAULT_RTSP_PORT = 8554;
constexpr uint32_t MAX_RTP_PAYLOAD   = 1300; // 单包最大 RTP 载荷（MTU 1500 安全上限）
constexpr uint8_t  RTP_PT_VIDEO      = 96;   // H.264 dynamic payload type
constexpr uint8_t  RTP_PT_AUDIO      = 97;   // AAC-hbr dynamic payload type
constexpr uint32_t VIDEO_CLOCK_RATE  = 90000;

// Base64 编码工具（用于 SDP 的 sprop-parameter-sets）
namespace rtsp_base64 {
    std::string encode(const uint8_t* data, size_t len);
    inline std::string encode(const char* data, size_t len) {
        return encode(reinterpret_cast<const uint8_t*>(data), len);
    }
    inline std::string encode(const std::string& s) {
        return encode(s.data(), s.size());
    }
}

// URL 解码与路径规范化工具
namespace rtsp_url {
    std::string decode(const std::string& str);
    // 从 RTSP URL (如 rtsp://ip:8554/stream//home/u/test.mp4) 提取文件绝对路径
    std::string extract_file_path(const std::string& url);
}

// 统一的轻量级日志宏
#define RTSP_LOG(fmt, ...) printf("[RtspServer] " fmt "\n", ##__VA_ARGS__)
#define RTSP_ERR(fmt, ...) fprintf(stderr, "[RtspServer ERROR] " fmt "\n", ##__VA_ARGS__)

#endif
