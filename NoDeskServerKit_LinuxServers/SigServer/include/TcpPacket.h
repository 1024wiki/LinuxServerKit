#ifndef TCPPACKET_H_
#define TCPPACKET_H_

#include "define.h"

// 信令帧编解码工具（规范 §2.1/§2.3）：
//   帧 = 0xFEFF(2) + DataLen(4,BE) + MsgId(2,BE) + Body(N) + Checksum(2,BE)
//   DataLen = MsgId(2) + Body(N) + Checksum(2)，不含 StartFlag 与自身
//   Checksum = (0xFE + 0xFF + DataLen四字节 + MsgId两字节 + Body各字节) & 0xFFFF
class TcpPacket
{
public:
    enum class ParseResult : int {
        PARSED      = 0,  // 成功解析出一帧（id/body 已填充）
        NEED_MORE   = 1,  // 数据不足，等待更多字节（可能已消耗垃圾字节）
        BAD_FRAME   = 2,  // 校验失败/帧错误（该帧已丢弃，可继续重扫）
    };

private:
    // 包含的一帧数据（投递逻辑队列后由处理器读取）
    DataId _id = DataId::NO_MESSAGE;
    std::shared_ptr<char[]> _body;   // JSON 载荷（UTF-8，已 \0 结尾）
    uint32_t _body_len = 0;

public:
    // 由解析结果构造（SigConnection 解析出完整帧后创建）
    TcpPacket(DataId id, std::shared_ptr<char[]> body, uint32_t body_len)
        : _id(id), _body(body), _body_len(body_len) {}
    TcpPacket() = default;

    // 访问器（与 SigServers/TcpPacket 接口对齐，供逻辑线程使用）
    DataId get_messageid() const { return _id; }
    std::shared_ptr<char[]> get_messagedata() const { return _body; }
    uint32_t get_messagelength() const { return _body_len; }

    // 构造发送帧：body 为 UTF-8 JSON 文本（可空）
    // 输出完整帧字节流与总长度，供 TcpConnection::Send(total, data) 使用
    static std::shared_ptr<char[]> Build(DataId id, const char* body, uint32_t body_len, uint32_t& out_total);
    static std::shared_ptr<char[]> Build(DataId id, const std::string& body, uint32_t& out_total) {
        return Build(id, body.data(), (uint32_t)body.size(), out_total);
    }

    // 从读缓冲区当前位置尝试解析一帧（规范 §2.4 状态机单步）
    //   - 未找到头：扫描 0xFE 0xFF，命中前丢弃；尾部孤立 0xFE 保留 → NEED_MORE
    //   - 头已找到但不足 → NEED_MORE（不消耗已找到的头）
    //   - 校验失败 → 丢弃该帧，返回 BAD_FRAME（调用方应继续循环解析）
    //   - 成功 → 消耗整帧，返回 PARSED
    // buf 需具备 ReadStartPos()/ReadableDataSize()/OffsetStartPos() 接口（ReactorTCPNet::ReadBuf 满足）
    template <typename ReadBufT>
    static ParseResult Parse(ReadBufT& buf, DataId& id, std::shared_ptr<char[]>& body, uint32_t& body_len)
    {
        while (true) {
            uint32_t avail = buf.ReadableDataSize();
            if (avail < START_FLAG_LEN)
                return ParseResult::NEED_MORE;

            const char* p = buf.ReadStartPos();
            // 1. 扫描起始标记 0xFE 0xFF
            uint32_t i = 0;
            bool found = false;
            for (; i + 1 < avail; ++i) {
                if ((uint8_t)p[i] == HEAD_FLAG_FIRST && (uint8_t)p[i + 1] == HEAD_FLAG_SECOND) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // 未找到：保留末尾可能的孤立 0xFE，其余全部丢弃
                uint32_t discard = avail;
                if ((uint8_t)p[avail - 1] == HEAD_FLAG_FIRST)
                    discard = avail - 1;
                buf.OffsetStartPos(discard);
                return ParseResult::NEED_MORE;
            }
            if (i > 0)
                buf.OffsetStartPos(i);          // 丢弃标记前垃圾字节
            avail = buf.ReadableDataSize();
            p = buf.ReadStartPos();

            // 2. 已找到头，等待 DataLen(4)
            if (avail < FRAME_HEAD_LEN)
                return ParseResult::NEED_MORE;

            // 3. 读 DataLen（BE → host）
            uint32_t data_len =
                ((uint32_t)(uint8_t)p[START_FLAG_LEN + 0] << 24) |
                ((uint32_t)(uint8_t)p[START_FLAG_LEN + 1] << 16) |
                ((uint32_t)(uint8_t)p[START_FLAG_LEN + 2] << 8)  |
                ((uint32_t)(uint8_t)p[START_FLAG_LEN + 3]);
            // data_len = MsgId(2) + Body(N) + Checksum(2)，至少 4
            if (data_len < FRAME_TAIL_LEN) {
                buf.OffsetStartPos(START_FLAG_LEN); // 非法长度：丢掉头两个字节重新扫描
                continue;
            }

            // 4. 等待整帧到达
            if (avail < FRAME_HEAD_LEN + data_len)
                return ParseResult::NEED_MORE;

            // 5. 校验 checksum（规范 §2.3）
            const char* payload = p + FRAME_HEAD_LEN;         // [MsgId|Body|Checksum]
            uint32_t body_n = data_len - FRAME_TAIL_LEN;
            uint16_t checksum_wire =
                (uint16_t)(((uint16_t)(uint8_t)payload[MSG_ID_LEN + body_n] << 8) |
                           (uint16_t)(uint8_t)payload[MSG_ID_LEN + body_n + 1]);

            uint32_t sum = HEAD_FLAG_FIRST + HEAD_FLAG_SECOND;
            for (uint32_t b = 0; b < DATA_LEN_FIELD; ++b)
                sum += (uint8_t)p[START_FLAG_LEN + b];        // DataLen 四字节原文
            for (uint32_t b = 0; b < MSG_ID_LEN + body_n; ++b)
                sum += (uint8_t)payload[b];                   // MsgId + Body
            uint16_t calc = (uint16_t)(sum & 0xFFFF);

            if (calc != checksum_wire) {
                // 校验失败：仅丢弃起始标记，重新扫描（防止误对齐丢数据）
                buf.OffsetStartPos(START_FLAG_LEN);
                return ParseResult::BAD_FRAME;
            }

            // 6. 解析成功：提取 MsgId 与 Body
            uint16_t mid = (uint16_t)(((uint16_t)(uint8_t)payload[0] << 8) | (uint8_t)payload[1]);
            id = (DataId)mid;
            body_len = body_n;
            if (body_n > 0) {
                body.reset(new char[body_n + 1]);
                memcpy(body.get(), payload + MSG_ID_LEN, body_n);
                body.get()[body_n] = '\0';
            } else {
                body.reset();
            }
            buf.OffsetStartPos(FRAME_HEAD_LEN + data_len);    // 消耗整帧
            return ParseResult::PARSED;
        }
    }

private:
    static uint16_t calc_checksum(const char* head6, const char* payload, uint32_t payload_len);
};

#endif
