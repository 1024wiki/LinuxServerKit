#ifndef PLAYCORE_H
#define PLAYCORE_H

#include "global.h"
#include <QObject>
#include <QString>
#include <functional>
#include <mutex>

#ifndef NO_VLC
// libVLC 头文件（仅在找到 libVLC SDK 时引入）
#include <vlc/vlc.h>
#endif

// PlayCore：基于 libVLC 原生 C API 封装的播放引擎模型类（Model）
// 对外提供面向对象的播放控制接口，并通过 Qt 信号槽将 libVLC 内部线程触发的事件
// 转发到 Qt 主线程，实现 UI 与播放引擎的观察者模式解耦。
// 若编译时未找到 libVLC SDK，则以 NO_VLC 宏编译为空实现，播放接口调用无效并返回失败。
class PlayCore : public QObject
{
    Q_OBJECT
public:
    explicit PlayCore(QObject* parent = nullptr);
    ~PlayCore();

    // 初始化播放器并设置渲染窗口句柄（Windows下为HWND，其他平台为QWidget句柄）
    bool init_player(quintptr render_hwnd);

    // 加载指定的RTSP URL媒体并开始播放
    bool load_media(const QString& rtsp_url);

    // 播放
    void play();
    // 暂停
    void pause();
    // 停止
    void stop();

    // 获取/设置当前播放时间(毫秒)
    qint64 get_time() const;
    void set_time(qint64 time_ms);

    // 获取总时长(毫秒)
    qint64 get_length() const;

    // 音量控制 (0~100)
    void set_volume(int volume);
    int get_volume() const;

    // 静音控制
    void set_mute(bool muted);
    bool is_muted() const;

signals:
    // 以下信号均在 libVLC 内部线程触发，通过 Qt::QueuedConnection 自动投递到连接槽函数所在线程执行
    void sig_time_changed(qint64 current_ms);
    void sig_length_changed(qint64 total_ms);
    void sig_state_changed(int state);      // 值为 PlayerStatus 枚举
    void sig_position_changed(float pos);   // 播放位置 0.0~1.0
    void sig_buffering(float percent);      // 缓冲进度百分比 0.0~100.0

private:
#ifndef NO_VLC
    // libVLC 核心对象
    libvlc_instance_t* vlc_instance;
    libvlc_media_player_t* player;
    libvlc_media_t* media;

    // libVLC 事件管理器与事件回调的 C 风格静态函数
    libvlc_event_manager_t* event_manager;
    static void vlc_event_callback(const libvlc_event_t* event, void* userdata);
#endif

    // 生命周期清理
    void cleanup();

    // 播放器内部状态跟踪
    PlayerStatus current_status;
};

#endif // PLAYCORE_H
