#include "DesktopStreamTask.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <unistd.h>

DesktopStreamTask::DesktopStreamTask(const std::string& client_ip)
    : StreamTask("desktop_live", client_ip)
{
}

DesktopStreamTask::~DesktopStreamTask() {
    Stop();
    CleanupEncoder();
    if (_disp) {
        XCloseDisplay(_disp);
        _disp = nullptr;
    }
}

bool DesktopStreamTask::InitDesktop() {
    _disp = XOpenDisplay(NULL);
    if (!_disp) {
        RTSP_ERR("DesktopStreamTask: XOpenDisplay failed, no X11 desktop environment available");
        return false;
    }

    _screen = DefaultScreen(_disp);
    _width = DisplayWidth(_disp, _screen);
    _height = DisplayHeight(_disp, _screen);

    // 宽高必须为偶数 (YUV420P 要求)
    if (_width % 2 != 0) _width -= 1;
    if (_height % 2 != 0) _height -= 1;

    _fps = 25;
    _video_fps = _fps;
    _duration_sec = 0.0; // 实时流无固定时长

    _has_video = true;
    _has_audio = false; // 实时桌面监控无音频流
    _tracks[0].stream_index = 0;
    _tracks[0].enabled = true;
    _tracks[1].enabled = false;

    if (!InitEncoder()) {
        RTSP_ERR("DesktopStreamTask: InitEncoder failed");
        return false;
    }

    RTSP_LOG("DesktopStreamTask: Desktop capture initialized: %dx%d @ %dfps", _width, _height, _fps);
    return true;
}

bool DesktopStreamTask::InitEncoder() {
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (!codec) {
        RTSP_ERR("DesktopStreamTask: H.264 encoder not found");
        return false;
    }

    _enc_ctx = avcodec_alloc_context3(codec);
    if (!_enc_ctx) return false;

    _enc_ctx->width = _width;
    _enc_ctx->height = _height;
    _enc_ctx->time_base = AVRational{1, _fps};
    _enc_ctx->framerate = AVRational{_fps, 1};
    _enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    _enc_ctx->gop_size = 25; // 1秒一个关键帧
    _enc_ctx->max_b_frames = 0; // 实时推流禁用 B 帧以降低时延
    _enc_ctx->bit_rate = 2000000; // 2Mbps 码率控制

    // 零延迟极速编码参数 (规范 §3.7 方案 B: zerolatency + ultrafast)
    av_opt_set(_enc_ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(_enc_ctx->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(_enc_ctx, codec, nullptr) < 0) {
        RTSP_ERR("DesktopStreamTask: avcodec_open2 failed");
        return false;
    }

    // 分配 YUV420P 编码帧
    _yuv_frame = av_frame_alloc();
    _yuv_frame->format = _enc_ctx->pix_fmt;
    _yuv_frame->width = _width;
    _yuv_frame->height = _height;
    if (av_image_alloc(_yuv_frame->data, _yuv_frame->linesize, _width, _height, _enc_ctx->pix_fmt, 32) < 0) {
        return false;
    }

    // RGB32/BGR0 -> YUV420P 转换上下文
    _sws_ctx = sws_getContext(_width, _height, AV_PIX_FMT_BGR0,
                              _width, _height, AV_PIX_FMT_YUV420P,
                              SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    return true;
}

void DesktopStreamTask::CleanupEncoder() {
    if (_sws_ctx) {
        sws_freeContext(_sws_ctx);
        _sws_ctx = nullptr;
    }
    if (_yuv_frame) {
        av_freep(&_yuv_frame->data[0]);
        av_frame_free(&_yuv_frame);
    }
    if (_enc_ctx) {
        avcodec_free_context(&_enc_ctx);
    }
}

void DesktopStreamTask::Play(double start_sec) {
    (void)start_sec;
    if (_running.load()) return;

    _running.store(true);
    _paused.store(false);

    // 启动桌面实时抓屏推流线程
    _worker_thread = std::thread(&DesktopStreamTask::DesktopWorkerLoop, this);
}

void DesktopStreamTask::Stop() {
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

void DesktopStreamTask::DesktopWorkerLoop() {
    RTSP_LOG("DesktopStreamTask live worker started (%dx%d @ %dfps)", _width, _height, _fps);

    Window root = RootWindow(_disp, _screen);
    AVPacket* pkt = av_packet_alloc();

    int64_t frame_index = 0;
    auto start_time = std::chrono::steady_clock::now();
    const std::chrono::microseconds frame_duration(1000000 / _fps);

    while (_running.load()) {
        auto next_frame_time = start_time + frame_duration * frame_index;

        // 1. 抓取屏幕图像
        XImage* img = XGetImage(_disp, root, 0, 0, _width, _height, AllPlanes, ZPixmap);
        if (img) {
            // 2. 将 XImage 像素转换为 YUV420P
            const uint8_t* in_data[1] = { reinterpret_cast<const uint8_t*>(img->data) };
            int in_linesize[1] = { img->bytes_per_line };
            sws_scale(_sws_ctx, in_data, in_linesize, 0, _height,
                      _yuv_frame->data, _yuv_frame->linesize);

            XDestroyImage(img);

            _yuv_frame->pts = frame_index;

            // 3. 送入编码器
            if (avcodec_send_frame(_enc_ctx, _yuv_frame) >= 0) {
                while (avcodec_receive_packet(_enc_ctx, pkt) >= 0) {
                    uint32_t ts = static_cast<uint32_t>(frame_index * (VIDEO_CLOCK_RATE / _fps));

                    // 4. 解析 Annex B / NALU 单元并封装发送 RTP 包
                    const uint8_t* p = pkt->data;
                    int size = pkt->size;

                    // 解析 Annex B 起始码 (0x00 00 00 01 或 0x00 00 01)
                    while (size > 3) {
                        int start_len = 0;
                        if (p[0] == 0 && p[1] == 0 && p[2] == 1) start_len = 3;
                        else if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) start_len = 4;

                        if (start_len > 0) {
                            p += start_len;
                            size -= start_len;

                            // 寻找下一个 NALU
                            int next_nal = size;
                            for (int i = 0; i + 3 < size; ++i) {
                                if ((p[i] == 0 && p[i+1] == 0 && p[i+2] == 1) ||
                                    (p[i] == 0 && p[i+1] == 0 && p[i+2] == 0 && p[i+3] == 1)) {
                                    next_nal = i;
                                    break;
                                }
                            }

                            if (next_nal > 0) {
                                bool is_last = (next_nal == size);
                                SendNalu(p, next_nal, ts, is_last);
                                p += next_nal;
                                size -= next_nal;
                            }
                        } else {
                            p++;
                            size--;
                        }
                    }

                    av_packet_unref(pkt);
                }
            }
        }

        ++frame_index;

        // 时钟步进休眠
        auto now = std::chrono::steady_clock::now();
        if (next_frame_time > now) {
            std::this_thread::sleep_until(next_frame_time);
        }
    }

    av_packet_free(&pkt);
    RTSP_LOG("DesktopStreamTask live worker ended");
}
