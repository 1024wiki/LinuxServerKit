#include "RtpPacket.h"
#include <arpa/inet.h>
#include <cstring>

std::vector<uint8_t> RtpPacket::Build(uint8_t pt, uint16_t seq, uint32_t ts, uint32_t ssrc,
                                      bool marker, const uint8_t* payload, size_t payload_len)
{
    std::vector<uint8_t> packet(sizeof(RtpHeader) + payload_len);
    RtpHeader* hdr = reinterpret_cast<RtpHeader*>(packet.data());

    hdr->version = 2;
    hdr->padding = 0;
    hdr->extension = 0;
    hdr->csrc_len = 0;
    hdr->marker = marker ? 1 : 0;
    hdr->payload_type = pt & 0x7F;
    hdr->seq_num = htons(seq);
    hdr->timestamp = htonl(ts);
    hdr->ssrc = htonl(ssrc);

    if (payload_len > 0 && payload != nullptr) {
        memcpy(packet.data() + sizeof(RtpHeader), payload, payload_len);
    }
    return packet;
}
