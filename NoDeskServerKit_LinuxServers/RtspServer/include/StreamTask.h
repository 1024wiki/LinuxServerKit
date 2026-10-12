#ifndef STREAM_TASK_H_
#define STREAM_TASK_H_

#include "RtspCommon.h"
#include <atomic>
#include <thread>
#include <mutex>

struct AVFormatContext;
struct AVPacket;

// 单条媒体轨道信息
struct TrackInfo {
    int track_id = -1; // 0=video, 1=audio
    int stream_index = -1; // FFmpeg stream index
    bool enabled = false;

    // 客户端/服务端 UDP 通道
    int client_rtp_port = 0;
    int client_rtcp_port = 0;
    int server_rtp_sock = -1;
    int server_rtcp_sock = -1;
    uint16_t server_rtp_port = 0;
    uint16_t server_rtcp_port = 0;
    sockaddr_in client_rtp_addr{};
    sockaddr_in client_rtcp_addr{};

    // RTP 流水线状态
    uint16_t seq = 0;
    uint32_t ts = 0;
    uint32_t ssrc = 0;
};

// 推流工作单元：在独立工作线程中执行解复用与 RTP 发送
class StreamTask {
protected:
    std::string _file_path;
    std::string _client_ip;
    TrackInfo _tracks[2]; // 0=video, 1=audio
    bool _has_video = false;
    bool _has_audio = false;

    std::atomic<bool> _running{false};
    std::atomic<bool> _paused{false};
    std::atomic<bool> _seek_requested{false};
    std::atomic<double> _seek_target_sec{0.0};

    std::thread _worker_thread;

    // 提取的媒体参数
    std::string _sps_base64;
    std::string _pps_base64;
    std::vector<uint8_t> _sps_raw;
    std::vector<uint8_t> _pps_raw;
    std::string _audio_config_hex;
    int _audio_sample_rate = 48000;
    int _audio_channels = 2;
    double _video_fps = 25.0;
    double _duration_sec = 0.0;
    int _avcc_length_size = 4; // AVCC NALU 长度前缀字节数

    void SendNalu(const uint8_t* nalu_data, size_t nalu_len, uint32_t timestamp, bool is_last_nalu);

private:
    void WorkerLoop();
    void SendH264Packet(AVPacket* pkt, int64_t pts0_v, std::chrono::steady_clock::time_point t0);
    void SendAacPacket(AVPacket* pkt, int64_t pts0_a, std::chrono::steady_clock::time_point t0);

public:
    StreamTask(const std::string& file_path, const std::string& client_ip);
    virtual ~StreamTask();

    // 初始化 FFmpeg 上下文并探测轨道参数与 extradata (SPS/PPS/AudioConfig)
    virtual bool InitMedia();

    // 生成 SDP 字符串 (用于 DESCRIBE 响应)
    virtual std::string GenerateSdp(const std::string& rtsp_url);

    // 为轨道绑定端口和套接字
    virtual bool SetupTrack(int track_id, int client_rtp_port, int client_rtcp_port,
                            uint16_t& out_server_rtp_port, uint16_t& out_server_rtcp_port);

    // 控制操作
    virtual void Play(double start_sec = 0.0);
    virtual void Pause();
    virtual void Seek(double sec);
    virtual void Stop();

    double GetDuration() const { return _duration_sec; }
    bool HasVideo() const { return _has_video; }
    bool HasAudio() const { return _has_audio; }
};

#endif
