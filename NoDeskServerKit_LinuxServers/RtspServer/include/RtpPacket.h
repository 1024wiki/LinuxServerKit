#ifndef RTP_PACKET_H_
#define RTP_PACKET_H_

#include "RtspCommon.h"

// 12 字节标准固定 RTP 头部 (RFC 3550)
struct RtpHeader {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t csrc_len:4;
    uint8_t extension:1;
    uint8_t padding:1;
    uint8_t version:2;
    uint8_t payload_type:7;
    uint8_t marker:1;
#else
    uint8_t version:2;
    uint8_t padding:1;
    uint8_t extension:1;
    uint8_t csrc_len:4;
    uint8_t marker:1;
    uint8_t payload_type:7;
#endif
    uint16_t seq_num;
    uint32_t timestamp;
    uint32_t ssrc;
} __attribute__((packed));

class RtpPacket {
public:
    // 构造带有 RTP 头部的网络缓冲区
    // 返回包含 12 字节头的完整数据包
    static std::vector<uint8_t> Build(uint8_t pt, uint16_t seq, uint32_t ts, uint32_t ssrc,
                                      bool marker, const uint8_t* payload, size_t payload_len);
};

#endif
