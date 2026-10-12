#ifndef DESKTOP_STREAM_TASK_H_
#define DESKTOP_STREAM_TASK_H_

#include "StreamTask.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

// 方案 B：实时桌面采集 (X11 XGetImage) -> H.264 硬件/软件实时编码 -> RTP/UDP 推流 (/desktop)
class DesktopStreamTask : public StreamTask {
private:
    Display* _disp = nullptr;
    int _screen = 0;
    int _width = 1280;
    int _height = 720;
    int _fps = 25;

    // 编码上下文
    AVCodecContext* _enc_ctx = nullptr;
    SwsContext* _sws_ctx = nullptr;
    AVFrame* _yuv_frame = nullptr;

    bool InitEncoder();
    void CleanupEncoder();

    // 覆盖 WorkerLoop 执行实时截屏与实时编码
    void DesktopWorkerLoop();

public:
    DesktopStreamTask(const std::string& client_ip);
    virtual ~DesktopStreamTask();

    // 初始化桌面捕获与实时编码参数
    bool InitDesktop();

    // 重写控制接口
    void Play(double start_sec = 0.0);
    void Stop();
};

#endif
