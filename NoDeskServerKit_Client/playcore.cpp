#include "playcore.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <QDir>

#ifdef NO_VLC
// ==================== NO_VLC 空实现(未找到 libVLC SDK 时编译) ====================
// 接口保留但不执行任何操作，init_player/load_media 返回失败，由播放窗口提示用户。

PlayCore::PlayCore(QObject* parent)
    : QObject(parent), current_status(PlayerStatus::STATUS_IDLE)
{
    qWarning() << "PlayCore: 当前以 NO_VLC 模式编译(未找到 libVLC SDK)，流媒体播放功能不可用。";
}

PlayCore::~PlayCore()
{
    cleanup();
}

void PlayCore::cleanup()
{
    current_status = PlayerStatus::STATUS_IDLE;
}

bool PlayCore::init_player(quintptr render_hwnd)
{
    Q_UNUSED(render_hwnd);
    qWarning() << "PlayCore: NO_VLC 模式，播放器初始化失败。";
    return false;
}

bool PlayCore::load_media(const QString& rtsp_url)
{
    Q_UNUSED(rtsp_url);
    return false;
}

void PlayCore::play() {}
void PlayCore::pause() {}
void PlayCore::stop() {}

qint64 PlayCore::get_time() const { return 0; }
void PlayCore::set_time(qint64 time_ms) { Q_UNUSED(time_ms); }
qint64 PlayCore::get_length() const { return 0; }

void PlayCore::set_volume(int volume) { Q_UNUSED(volume); }
int PlayCore::get_volume() const { return 0; }

void PlayCore::set_mute(bool muted) { Q_UNUSED(muted); }
bool PlayCore::is_muted() const { return false; }

#else
// ==================== libVLC 正常实现 ====================

PlayCore::PlayCore(QObject* parent)
    : QObject(parent),
      vlc_instance(nullptr),
      player(nullptr),
      media(nullptr),
      event_manager(nullptr),
      current_status(PlayerStatus::STATUS_IDLE)
{
    // 自动探测 VLC plugins 目录（优先使用同级 plugins 或工程 vlc_sdk/plugins 64位插件目录）
    QString app_dir = QCoreApplication::applicationDirPath();
    QString local_plugins = app_dir + "/plugins";
    if (!QDir(local_plugins).exists() && qEnvironmentVariableIsEmpty("VLC_PLUGIN_PATH")) {
        QStringList candidates = {
            QDir::cleanPath(app_dir + "/../../vlc_sdk/plugins"),
            QDir::cleanPath(app_dir + "/../../../vlc_sdk/plugins"),
            "D:/Edaoyun_c++/Project/Remote_Control/RemoteControl/remote-control-client/RemoteControlClient/vlc_sdk/plugins",
            "C:/Program Files/VideoLAN/VLC/plugins"
        };
        for (const QString& path : candidates) {
            if (QDir(path).exists()) {
                qputenv("VLC_PLUGIN_PATH", QDir::toNativeSeparators(path).toLocal8Bit());
                qDebug() << "PlayCore: 自动定位 64位 VLC_PLUGIN_PATH =" << path;
                break;
            }
        }
    }

    // 初始化 libVLC 实例，追加低延迟网络缓冲参数
    const char* const vlc_args[] = {
        VLC_NETWORK_CACHING,   // 网络拉流缓冲(ms)
        "--no-video-title-show" // 不在画面上叠加标题文字
    };
    int argc = sizeof(vlc_args) / sizeof(vlc_args[0]);
    vlc_instance = libvlc_new(argc, vlc_args);
    if (!vlc_instance) {
        qWarning() << "PlayCore: libvlc_new 初始化失败，请检查 libVLC DLL 与 plugins 是否已正确部署。";
        return;
    }
    qDebug() << "PlayCore: libVLC 实例初始化成功。";
}

PlayCore::~PlayCore()
{
    cleanup();
}

// 清理所有 libVLC 资源
void PlayCore::cleanup()
{
    if (player) {
        libvlc_media_player_stop(player);
        if (event_manager) {
            libvlc_event_detach(event_manager, libvlc_MediaPlayerTimeChanged, &PlayCore::vlc_event_callback, this);
            libvlc_event_detach(event_manager, libvlc_MediaPlayerLengthChanged, &PlayCore::vlc_event_callback, this);
            libvlc_event_detach(event_manager, libvlc_MediaPlayerPositionChanged, &PlayCore::vlc_event_callback, this);
            libvlc_event_detach(event_manager, libvlc_MediaPlayerBuffering, &PlayCore::vlc_event_callback, this);
            libvlc_event_detach(event_manager, libvlc_MediaPlayerOpening, &PlayCore::vlc_event_callback, this);
            libvlc_event_detach(event_manager, libvlc_MediaPlayerPlaying, &PlayCore::vlc_event_callback, this);
            libvlc_event_detach(event_manager, libvlc_MediaPlayerPaused, &PlayCore::vlc_event_callback, this);
            libvlc_event_detach(event_manager, libvlc_MediaPlayerStopped, &PlayCore::vlc_event_callback, this);
            libvlc_event_detach(event_manager, libvlc_MediaPlayerEndReached, &PlayCore::vlc_event_callback, this);
            libvlc_event_detach(event_manager, libvlc_MediaPlayerEncounteredError, &PlayCore::vlc_event_callback, this);
            event_manager = nullptr;
        }
        libvlc_media_player_release(player);
        player = nullptr;
    }
    if (media) {
        libvlc_media_release(media);
        media = nullptr;
    }
    if (vlc_instance) {
        libvlc_release(vlc_instance);
        vlc_instance = nullptr;
    }
}

// 初始化播放器并绑定渲染窗口句柄
bool PlayCore::init_player(quintptr render_hwnd)
{
    if (!vlc_instance) {
        qWarning() << "PlayCore: libVLC 实例为空，初始化播放器失败。";
        return false;
    }

    // 创建播放器实例（不绑定媒体，后续通过 load_media 设置）
    if (!player) {
        player = libvlc_media_player_new(vlc_instance);
        if (!player) {
            qWarning() << "PlayCore: libvlc_media_player_new 创建播放器失败。";
            return false;
        }
    }

    // 绑定渲染窗口句柄：Windows 下为 HWND，其他平台（X11/Linux）为 Window/Display
#ifdef Q_OS_WIN
    libvlc_media_player_set_hwnd(player, reinterpret_cast<void*>(render_hwnd));
#else
    libvlc_media_player_set_xwindow(player, static_cast<uint32_t>(render_hwnd));
#endif

    // 获取事件管理器并注册 libVLC 事件回调（观察者模式：libVLC 播放器为被观察者）
    event_manager = libvlc_media_player_event_manager(player);
    if (event_manager) {
        libvlc_event_attach(event_manager, libvlc_MediaPlayerTimeChanged, &PlayCore::vlc_event_callback, this);
        libvlc_event_attach(event_manager, libvlc_MediaPlayerLengthChanged, &PlayCore::vlc_event_callback, this);
        libvlc_event_attach(event_manager, libvlc_MediaPlayerPositionChanged, &PlayCore::vlc_event_callback, this);
        libvlc_event_attach(event_manager, libvlc_MediaPlayerBuffering, &PlayCore::vlc_event_callback, this);
        libvlc_event_attach(event_manager, libvlc_MediaPlayerOpening, &PlayCore::vlc_event_callback, this);
        libvlc_event_attach(event_manager, libvlc_MediaPlayerPlaying, &PlayCore::vlc_event_callback, this);
        libvlc_event_attach(event_manager, libvlc_MediaPlayerPaused, &PlayCore::vlc_event_callback, this);
        libvlc_event_attach(event_manager, libvlc_MediaPlayerStopped, &PlayCore::vlc_event_callback, this);
        libvlc_event_attach(event_manager, libvlc_MediaPlayerEndReached, &PlayCore::vlc_event_callback, this);
        libvlc_event_attach(event_manager, libvlc_MediaPlayerEncounteredError, &PlayCore::vlc_event_callback, this);
    }

    qDebug() << "PlayCore: 播放器初始化成功，渲染句柄：" << render_hwnd;
    return true;
}

// 加载 RTSP URL 媒体并开始播放
bool PlayCore::load_media(const QString& rtsp_url)
{
    if (!vlc_instance || !player) {
        qWarning() << "PlayCore: 播放器未初始化，无法加载媒体。";
        return false;
    }
    if (rtsp_url.isEmpty()) {
        qWarning() << "PlayCore: 媒体URL为空。";
        return false;
    }

    // 释放旧媒体引用
    if (media) {
        libvlc_media_release(media);
        media = nullptr;
    }

    // 通过 URL 创建媒体（libVLC 内部封装 RTSP 协议处理）
    // 注意：libvlc_media_new_location 对网络流比 libvlc_media_new_path 更合适
    QByteArray url_bytes = rtsp_url.toUtf8();
    media = libvlc_media_new_location(vlc_instance, url_bytes.constData());
    if (!media) {
        qWarning() << "PlayCore: 创建媒体失败，URL：" << rtsp_url;
        return false;
    }

    // 设置媒体播放完毕后不自动重复
    libvlc_media_add_option(media, ":input-repeat=0");

    // 关联媒体到播放器
    libvlc_media_player_set_media(player, media);

    qDebug() << "PlayCore: 成功加载媒体 URL：" << rtsp_url;
    return true;
}

// 播放
void PlayCore::play()
{
    if (!player) return;
    int ret = libvlc_media_player_play(player);
    if (ret != 0)
        qWarning() << "PlayCore: libvlc_media_player_play 调用失败。";
}

// 暂停
void PlayCore::pause()
{
    if (!player) return;
    libvlc_media_player_pause(player);
}

// 停止
void PlayCore::stop()
{
    if (!player) return;
    libvlc_media_player_stop(player);
}

// 获取当前播放时间(ms)
qint64 PlayCore::get_time() const
{
    if (!player) return 0;
    return static_cast<qint64>(libvlc_media_player_get_time(player));
}

// 设置当前播放时间(ms)
void PlayCore::set_time(qint64 time_ms)
{
    if (!player) return;
    libvlc_media_player_set_time(player, static_cast<libvlc_time_t>(time_ms));
}

// 获取总时长(ms)
qint64 PlayCore::get_length() const
{
    if (!player) return 0;
    return static_cast<qint64>(libvlc_media_player_get_length(player));
}

// 设置音量(0~100)
void PlayCore::set_volume(int volume)
{
    if (!player) return;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    libvlc_audio_set_volume(player, volume);
}

// 获取音量
int PlayCore::get_volume() const
{
    if (!player) return 0;
    return libvlc_audio_get_volume(player);
}

// 静音设置
void PlayCore::set_mute(bool muted)
{
    if (!player) return;
    libvlc_audio_set_mute(player, muted ? 1 : 0);
}

// 获取静音状态
bool PlayCore::is_muted() const
{
    if (!player) return false;
    return libvlc_audio_get_mute(player) != 0;
}

// libVLC 事件回调静态函数（在 libVLC 内部线程触发）
// 通过 QObject::metaObject()->invokeMethod 或直接 emit 信号，Qt::QueuedConnection
// 机制保证接收槽函数在正确的线程中执行
void PlayCore::vlc_event_callback(const libvlc_event_t* event, void* userdata)
{
    PlayCore* self = static_cast<PlayCore*>(userdata);
    if (!self || !event) return;

    switch (event->type) {
    case libvlc_MediaPlayerTimeChanged: {
        // 播放时间变化事件
        qint64 current_ms = static_cast<qint64>(event->u.media_player_time_changed.new_time);
        emit self->sig_time_changed(current_ms);
        break;
    }
    case libvlc_MediaPlayerLengthChanged: {
        // 媒体总时长变化事件
        qint64 total_ms = static_cast<qint64>(event->u.media_player_length_changed.new_length);
        emit self->sig_length_changed(total_ms);
        break;
    }
    case libvlc_MediaPlayerPositionChanged: {
        // 播放位置(百分比)变化事件
        float pos = event->u.media_player_position_changed.new_position;
        emit self->sig_position_changed(pos);
        break;
    }
    case libvlc_MediaPlayerBuffering: {
        // 缓冲进度事件
        float percent = event->u.media_player_buffering.new_cache;
        emit self->sig_buffering(percent);
        break;
    }
    case libvlc_MediaPlayerOpening:
        self->current_status = PlayerStatus::STATUS_OPENING;
        emit self->sig_state_changed(static_cast<int>(self->current_status));
        break;
    case libvlc_MediaPlayerPlaying:
        self->current_status = PlayerStatus::STATUS_PLAYING;
        emit self->sig_state_changed(static_cast<int>(self->current_status));
        break;
    case libvlc_MediaPlayerPaused:
        self->current_status = PlayerStatus::STATUS_PAUSED;
        emit self->sig_state_changed(static_cast<int>(self->current_status));
        break;
    case libvlc_MediaPlayerStopped:
        self->current_status = PlayerStatus::STATUS_STOPPED;
        emit self->sig_state_changed(static_cast<int>(self->current_status));
        break;
    case libvlc_MediaPlayerEndReached:
        self->current_status = PlayerStatus::STATUS_ENDED;
        emit self->sig_state_changed(static_cast<int>(self->current_status));
        break;
    case libvlc_MediaPlayerEncounteredError:
        self->current_status = PlayerStatus::STATUS_ERROR;
        emit self->sig_state_changed(static_cast<int>(self->current_status));
        break;
    default:
        break;
    }
}

#endif // NO_VLC
