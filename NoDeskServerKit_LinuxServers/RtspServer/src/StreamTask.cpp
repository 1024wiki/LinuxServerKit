#include "StreamTask.h"
#include "RtpPacket.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
}

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <random>

StreamTask::StreamTask(const std::string& file_path, const std::string& client_ip)
    : _file_path(file_path), _client_ip(client_ip)
{
    // 初始化轨道默认 SSRC
    std::random_device rd;
    std::mt19937 gen(rd());
    _tracks[0].ssrc = gen();
    _tracks[1].ssrc = gen();
    _tracks[0].track_id = 0;
    _tracks[1].track_id = 1;
}

StreamTask::~StreamTask() {
    Stop();
}

bool StreamTask::InitMedia() {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, _file_path.c_str(), nullptr, nullptr) < 0) {
        RTSP_ERR("avformat_open_input failed: %s", _file_path.c_str());
        return false;
    }

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        RTSP_ERR("avformat_find_stream_info failed: %s", _file_path.c_str());
        avformat_close_input(&fmt);
        return false;
    }

    if (fmt->duration > 0) {
        _duration_sec = static_cast<double>(fmt->duration) / AV_TIME_BASE;
    }

    for (unsigned int i = 0; i < fmt->nb_streams; ++i) {
        AVStream* st = fmt->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !_has_video) {
            if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
                _has_video = true;
                _tracks[0].stream_index = i;
                _tracks[0].enabled = true;

                // 帧率计算
                if (st->avg_frame_rate.den > 0 && st->avg_frame_rate.num > 0) {
                    _video_fps = av_q2d(st->avg_frame_rate);
                } else if (st->r_frame_rate.den > 0 && st->r_frame_rate.num > 0) {
                    _video_fps = av_q2d(st->r_frame_rate);
                }
                if (_video_fps < 1.0 || _video_fps > 120.0) _video_fps = 25.0;

                // 解析 extradata 中的 AVCDecoderConfigurationRecord 提取 SPS/PPS
                uint8_t* extra = st->codecpar->extradata;
                int extra_size = st->codecpar->extradata_size;
                if (extra && extra_size >= 7 && extra[0] == 1) {
                    // AVCC 格式
                    _avcc_length_size = (extra[4] & 0x03) + 1;
                    int num_sps = extra[5] & 0x1F;
                    int offset = 6;
                    if (num_sps > 0 && offset + 2 <= extra_size) {
                        int sps_len = (extra[offset] << 8) | extra[offset + 1];
                        offset += 2;
                        if (offset + sps_len <= extra_size) {
                            _sps_raw.assign(extra + offset, extra + offset + sps_len);
                            _sps_base64 = rtsp_base64::encode(_sps_raw.data(), _sps_raw.size());
                            offset += sps_len;
                        }
                    }
                    if (offset < extra_size) {
                        int num_pps = extra[offset++];
                        if (num_pps > 0 && offset + 2 <= extra_size) {
                            int pps_len = (extra[offset] << 8) | extra[offset + 1];
                            offset += 2;
                            if (offset + pps_len <= extra_size) {
                                _pps_raw.assign(extra + offset, extra + offset + pps_len);
                                _pps_base64 = rtsp_base64::encode(_pps_raw.data(), _pps_raw.size());
                            }
                        }
                    }
                }
                RTSP_LOG("Video: H264, fps=%.2f, duration=%.2fs, sps_len=%zu, pps_len=%zu",
                         _video_fps, _duration_sec, _sps_raw.size(), _pps_raw.size());
            }
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !_has_audio) {
            if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
                _has_audio = true;
                _tracks[1].stream_index = i;
                _tracks[1].enabled = true;
                _audio_sample_rate = st->codecpar->sample_rate ? st->codecpar->sample_rate : 48000;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 24, 100)
                _audio_channels = st->codecpar->ch_layout.nb_channels ? st->codecpar->ch_layout.nb_channels : 2;
#else
                _audio_channels = st->codecpar->channels ? st->codecpar->channels : 2;
#endif

                uint8_t* extra = st->codecpar->extradata;
                int extra_size = st->codecpar->extradata_size;
                if (extra && extra_size >= 2) {
                    char hex_buf[16];
                    snprintf(hex_buf, sizeof(hex_buf), "%02X%02X", extra[0], extra[1]);
                    _audio_config_hex = hex_buf;
                } else {
                    _audio_config_hex = "1210"; // 默认 48kHz, 2ch, LC
                }
                RTSP_LOG("Audio: AAC, sample_rate=%d, ch=%d, config=%s",
                         _audio_sample_rate, _audio_channels, _audio_config_hex.c_str());
            }
        }
    }

    avformat_close_input(&fmt);
    return (_has_video || _has_audio);
}

std::string StreamTask::GenerateSdp(const std::string& rtsp_url) {
    std::ostringstream sdp;
    sdp << "v=0\r\n";
    sdp << "o=- 0 0 IN IP4 0.0.0.0\r\n";
    sdp << "s=NoDeskServerKit\r\n";
    sdp << "t=0 0\r\n";
    sdp << "a=control:*\r\n";
    if (_duration_sec > 0.0) {
        sdp << "a=range:npt=0-" << std::fixed << std::setprecision(3) << _duration_sec << "\r\n";
    }

    if (_has_video) {
        sdp << "m=video 0 RTP/AVP " << (int)RTP_PT_VIDEO << "\r\n";
        sdp << "a=rtpmap:" << (int)RTP_PT_VIDEO << " H264/" << VIDEO_CLOCK_RATE << "\r\n";
        sdp << "a=fmtp:" << (int)RTP_PT_VIDEO << " profile-level-id=42C01F;packetization-mode=1";
        if (!_sps_base64.empty() && !_pps_base64.empty()) {
            sdp << ";sprop-parameter-sets=" << _sps_base64 << "," << _pps_base64;
        }
        sdp << "\r\n";
        sdp << "a=control:trackID=0\r\n";
    }

    if (_has_audio) {
        sdp << "m=audio 0 RTP/AVP " << (int)RTP_PT_AUDIO << "\r\n";
        sdp << "a=rtpmap:" << (int)RTP_PT_AUDIO << " mpeg4-generic/" << _audio_sample_rate << "/" << _audio_channels << "\r\n";
        sdp << "a=fmtp:" << (int)RTP_PT_AUDIO << " streamType=5; profile-level-id=1; mode=AAC-hbr; config="
            << _audio_config_hex << "; SizeLength=13; IndexLength=3; IndexDeltaLength=3\r\n";
        sdp << "a=control:trackID=1\r\n";
    }

    return sdp.str();
}

bool StreamTask::SetupTrack(int track_id, int client_rtp_port, int client_rtcp_port,
                            uint16_t& out_server_rtp_port, uint16_t& out_server_rtcp_port)
{
    if (track_id < 0 || track_id > 1) return false;
    TrackInfo& trk = _tracks[track_id];

    trk.client_rtp_port = client_rtp_port;
    trk.client_rtcp_port = client_rtcp_port;

    memset(&trk.client_rtp_addr, 0, sizeof(trk.client_rtp_addr));
    trk.client_rtp_addr.sin_family = AF_INET;
    trk.client_rtp_addr.sin_port = htons(client_rtp_port);
    inet_pton(AF_INET, _client_ip.c_str(), &trk.client_rtp_addr.sin_addr);

    memset(&trk.client_rtcp_addr, 0, sizeof(trk.client_rtcp_addr));
    trk.client_rtcp_addr.sin_family = AF_INET;
    trk.client_rtcp_addr.sin_port = htons(client_rtcp_port);
    inet_pton(AF_INET, _client_ip.c_str(), &trk.client_rtcp_addr.sin_addr);

    // 为服务端分配一对 UDP 端口 (RTP 偶数，RTCP 奇数)
    static std::atomic<uint16_t> port_alloc{20000};
    for (int retry = 0; retry < 50; ++retry) {
        uint16_t base = port_alloc.fetch_add(2);
        if (base > 40000) port_alloc.store(20000);

        int s_rtp = socket(AF_INET, SOCK_DGRAM, 0);
        int s_rtcp = socket(AF_INET, SOCK_DGRAM, 0);
        if (s_rtp < 0 || s_rtcp < 0) {
            if (s_rtp >= 0) close(s_rtp);
            if (s_rtcp >= 0) close(s_rtcp);
            continue;
        }

        sockaddr_in rtp_addr{}, rtcp_addr{};
        rtp_addr.sin_family = AF_INET;
        rtp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        rtp_addr.sin_port = htons(base);

        rtcp_addr.sin_family = AF_INET;
        rtcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        rtcp_addr.sin_port = htons(base + 1);

        if (bind(s_rtp, (sockaddr*)&rtp_addr, sizeof(rtp_addr)) == 0 &&
            bind(s_rtcp, (sockaddr*)&rtcp_addr, sizeof(rtcp_addr)) == 0)
        {
            trk.server_rtp_sock = s_rtp;
            trk.server_rtcp_sock = s_rtcp;
            trk.server_rtp_port = base;
            trk.server_rtcp_port = base + 1;
            out_server_rtp_port = base;
            out_server_rtcp_port = base + 1;
            return true;
        }
        close(s_rtp);
        close(s_rtcp);
    }
    return false;
}

void StreamTask::Play(double start_sec) {
    if (_paused.load()) {
        _paused.store(false);
        if (start_sec > 0.0) {
            Seek(start_sec);
        }
        return;
    }

    if (_running.load()) {
        if (start_sec > 0.0) {
            Seek(start_sec);
        }
        return;
    }

    _running.store(true);
    _paused.store(false);
    _seek_target_sec.store(start_sec);
    if (start_sec > 0.0) _seek_requested.store(true);

    _worker_thread = std::thread(&StreamTask::WorkerLoop, this);
}

void StreamTask::Pause() {
    _paused.store(true);
}

void StreamTask::Seek(double sec) {
    _seek_target_sec.store(sec);
    _seek_requested.store(true);
}

void StreamTask::Stop() {
    if (_running.load()) {
        _running.store(false);
        _paused.store(false);
        if (_worker_thread.joinable()) {
            _worker_thread.join();
        }
    }

    for (int i = 0; i < 2; ++i) {
        if (_tracks[i].server_rtp_sock >= 0) {
            close(_tracks[i].server_rtp_sock);
            _tracks[i].server_rtp_sock = -1;
        }
        if (_tracks[i].server_rtcp_sock >= 0) {
            close(_tracks[i].server_rtcp_sock);
            _tracks[i].server_rtcp_sock = -1;
        }
    }
}

void StreamTask::SendNalu(const uint8_t* nalu_data, size_t nalu_len, uint32_t timestamp, bool is_last_nalu) {
    if (nalu_len == 0 || !_tracks[0].enabled || _tracks[0].server_rtp_sock < 0) return;

    if (nalu_len <= MAX_RTP_PAYLOAD) {
        // 单 NALU 单元包
        auto pkt = RtpPacket::Build(RTP_PT_VIDEO, _tracks[0].seq++, timestamp,
                                    _tracks[0].ssrc, is_last_nalu, nalu_data, nalu_len);
        sendto(_tracks[0].server_rtp_sock, pkt.data(), pkt.size(), 0,
               (sockaddr*)&_tracks[0].client_rtp_addr, sizeof(_tracks[0].client_rtp_addr));
    } else {
        // FU-A 分片封装 (RFC 3984 / RFC 6184)
        uint8_t nal_head = nalu_data[0];
        uint8_t fu_indicator = (nal_head & 0xE0) | 28; // FU-A type = 28
        uint8_t nal_type = nal_head & 0x1F;

        const uint8_t* payload = nalu_data + 1;
        size_t remain = nalu_len - 1;
        bool is_first = true;

        while (remain > 0) {
            size_t chunk = std::min<size_t>(remain, MAX_RTP_PAYLOAD - 2);
            bool is_end = (remain == chunk);

            uint8_t fu_header = nal_type;
            if (is_first) fu_header |= 0x80; // S = 1
            if (is_end)   fu_header |= 0x40; // E = 1

            std::vector<uint8_t> fu_payload(2 + chunk);
            fu_payload[0] = fu_indicator;
            fu_payload[1] = fu_header;
            memcpy(fu_payload.data() + 2, payload, chunk);

            bool marker = (is_end && is_last_nalu);
            auto pkt = RtpPacket::Build(RTP_PT_VIDEO, _tracks[0].seq++, timestamp,
                                        _tracks[0].ssrc, marker, fu_payload.data(), fu_payload.size());
            sendto(_tracks[0].server_rtp_sock, pkt.data(), pkt.size(), 0,
                   (sockaddr*)&_tracks[0].client_rtp_addr, sizeof(_tracks[0].client_rtp_addr));

            payload += chunk;
            remain -= chunk;
            is_first = false;
        }
    }
}

void StreamTask::WorkerLoop() {
    RTSP_LOG("StreamTask worker thread started for %s", _file_path.c_str());

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, _file_path.c_str(), nullptr, nullptr) < 0) {
        RTSP_ERR("Worker: avformat_open_input failed: %s", _file_path.c_str());
        return;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        RTSP_ERR("Worker: avformat_find_stream_info failed");
        avformat_close_input(&fmt);
        return;
    }

    // 首包优先发送 SPS 与 PPS (保证客户端解码器秒开)
    uint32_t current_ts_v = 0;
    if (_has_video && !_sps_raw.empty() && !_pps_raw.empty()) {
        SendNalu(_sps_raw.data(), _sps_raw.size(), current_ts_v, false);
        SendNalu(_pps_raw.data(), _pps_raw.size(), current_ts_v, false);
    }

    AVPacket* pkt = av_packet_alloc();
    auto t0 = std::chrono::steady_clock::now();
    int64_t pts0_v = -1;
    int64_t pts0_a = -1;

    // 处理起始 Seek
    if (_seek_requested.load()) {
        double sec = _seek_target_sec.load();
        int64_t target_ts = static_cast<int64_t>(sec * AV_TIME_BASE);
        av_seek_frame(fmt, -1, target_ts, AVSEEK_FLAG_BACKWARD);
        avformat_flush(fmt);
        _seek_requested.store(false);
        t0 = std::chrono::steady_clock::now();
        pts0_v = -1;
        pts0_a = -1;
    }

    while (_running.load()) {
        if (_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (_seek_requested.load()) {
            double sec = _seek_target_sec.load();
            int64_t target_ts = static_cast<int64_t>(sec * AV_TIME_BASE);
            av_seek_frame(fmt, -1, target_ts, AVSEEK_FLAG_BACKWARD);
            avformat_flush(fmt);
            _seek_requested.store(false);
            t0 = std::chrono::steady_clock::now();
            pts0_v = -1;
            pts0_a = -1;
        }

        int ret = av_read_frame(fmt, pkt);
        if (ret < 0) {
            // 文件结束
            RTSP_LOG("StreamTask: EOF reached for %s", _file_path.c_str());
            break;
        }

        if (_has_video && pkt->stream_index == _tracks[0].stream_index) {
            AVStream* st = fmt->streams[pkt->stream_index];
            if (pts0_v < 0) pts0_v = pkt->pts;
            int64_t d_pts = pkt->pts - pts0_v;
            double elapsed_sec = d_pts * av_q2d(st->time_base);
            if (elapsed_sec > 0.0) {
                auto target_time = t0 + std::chrono::microseconds(static_cast<int64_t>(elapsed_sec * 1000000.0));
                auto now = std::chrono::steady_clock::now();
                if (target_time > now) {
                    std::this_thread::sleep_until(target_time);
                }
            }

            uint32_t ts = static_cast<uint32_t>(elapsed_sec * VIDEO_CLOCK_RATE);

            // AVCC 切割 NALU (按 4 字节前缀解析)
            const uint8_t* p = pkt->data;
            int size = pkt->size;
            while (size >= _avcc_length_size) {
                uint32_t nalu_len = 0;
                for (int i = 0; i < _avcc_length_size; ++i) {
                    nalu_len = (nalu_len << 8) | p[i];
                }
                p += _avcc_length_size;
                size -= _avcc_length_size;

                if (nalu_len > (uint32_t)size) break;

                bool is_last = (size == (int)nalu_len);
                SendNalu(p, nalu_len, ts, is_last);

                p += nalu_len;
                size -= nalu_len;
            }
        } else if (_has_audio && pkt->stream_index == _tracks[1].stream_index) {
            AVStream* st = fmt->streams[pkt->stream_index];
            if (pts0_a < 0) pts0_a = pkt->pts;
            int64_t d_pts = pkt->pts - pts0_a;
            double elapsed_sec = d_pts * av_q2d(st->time_base);
            if (elapsed_sec > 0.0) {
                auto target_time = t0 + std::chrono::microseconds(static_cast<int64_t>(elapsed_sec * 1000000.0));
                auto now = std::chrono::steady_clock::now();
                if (target_time > now) {
                    std::this_thread::sleep_until(target_time);
                }
            }

            uint32_t ts = static_cast<uint32_t>(elapsed_sec * _audio_sample_rate);

            // RFC 3640 AAC-hbr AU 封装: AU-headers-length (2字节=16) + AU-header (2字节) + raw AAC
            // AU-header: 13位 au_size, 3位 au_index=0
            if (pkt->size > 0 && _tracks[1].server_rtp_sock >= 0) {
                uint16_t au_size = pkt->size;
                uint16_t au_header = (au_size << 3) & 0xFFF8;

                std::vector<uint8_t> payload(4 + pkt->size);
                payload[0] = 0x00; // AU-headers-length high
                payload[1] = 0x10; // AU-headers-length low (16 bits)
                payload[2] = (au_header >> 8) & 0xFF;
                payload[3] = au_header & 0xFF;
                memcpy(payload.data() + 4, pkt->data, pkt->size);

                auto rtp_pkt = RtpPacket::Build(RTP_PT_AUDIO, _tracks[1].seq++, ts,
                                                _tracks[1].ssrc, true, payload.data(), payload.size());
                sendto(_tracks[1].server_rtp_sock, rtp_pkt.data(), rtp_pkt.size(), 0,
                       (sockaddr*)&_tracks[1].client_rtp_addr, sizeof(_tracks[1].client_rtp_addr));
            }
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    RTSP_LOG("StreamTask worker thread ended for %s", _file_path.c_str());
}
