#include "TcpPacket.h"
#include <arpa/inet.h>

// 计算校验和：0xFE + 0xFF + DataLen四字节原文 + MsgId两字节 + Body各字节，取低16位
uint16_t TcpPacket::calc_checksum(const char* head6, const char* payload, uint32_t payload_len)
{
    uint32_t sum = HEAD_FLAG_FIRST + HEAD_FLAG_SECOND;
    for (uint32_t i = 0; i < DATA_LEN_FIELD; ++i)
        sum += (uint8_t)head6[START_FLAG_LEN + i];   // DataLen 四字节
    for (uint32_t i = 0; i < payload_len; ++i)
        sum += (uint8_t)payload[i];                  // MsgId + Body
    return (uint16_t)(sum & 0xFFFF);
}

std::shared_ptr<char[]> TcpPacket::Build(DataId id, const char* body, uint32_t body_len, uint32_t& out_total)
{
    uint32_t data_len = MSG_ID_LEN + body_len + CHECKSUM_LEN; // = 2 + N + 2
    out_total = FRAME_HEAD_LEN + data_len;                    // 6 + data_len

    std::shared_ptr<char[]> frame(new char[out_total]);
    // Start Flag
    frame[0] = (char)HEAD_FLAG_FIRST;
    frame[1] = (char)HEAD_FLAG_SECOND;
    // DataLen (BE)
    uint32_t dl_be = htonl(data_len);
    memcpy(frame.get() + START_FLAG_LEN, &dl_be, DATA_LEN_FIELD);
    // MsgId (BE)
    uint16_t id_be = htons((uint16_t)id);
    memcpy(frame.get() + FRAME_HEAD_LEN, &id_be, MSG_ID_LEN);
    // Body
    if (body_len > 0 && body != nullptr)
        memcpy(frame.get() + FRAME_HEAD_LEN + MSG_ID_LEN, body, body_len);
    // Checksum (BE)：对 head6 + MsgId + Body 累加
    uint16_t csum = calc_checksum(frame.get(), frame.get() + FRAME_HEAD_LEN, MSG_ID_LEN + body_len);
    uint16_t csum_be = htons(csum);
    memcpy(frame.get() + FRAME_HEAD_LEN + MSG_ID_LEN + body_len, &csum_be, CHECKSUM_LEN);

    return frame;
}
