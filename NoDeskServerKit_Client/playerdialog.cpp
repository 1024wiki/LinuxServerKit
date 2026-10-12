#include "playerdialog.h"
#include "ui_playerdialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QFrame>
#include <QStyle>
#include <QDebug>

// ======================== 悬浮控制条子部件封装 ========================
// 为避免直接在.ui中嵌入覆盖在原生HWND渲染层上的控件(会被Direct3D表面遮挡)，
// 这里以代码方式构建一个透明背景的悬浮容器QWidget，浮动在video_area之上。

PlayerDialog::PlayerDialog(QWidget *parent)
    : QDialog(parent),
      ui(new Ui::PlayerDialog),
      playcore(nullptr),
      hide_timer(nullptr),
      user_seeking(false),
      muted(false),
      last_volume(80)
{
    ui->setupUi(this);

    // 保证video_area能够获取原生窗口句柄
    ui->video_area->setAttribute(Qt::WA_NativeWindow, true);

    // 初始化播放引擎（Model层）
    playcore = new PlayCore(this);

    // ======== 构建悬浮控制条 ========
    QFrame* controlbar = new QFrame(ui->video_area);
    controlbar->setObjectName("controlbar");
    controlbar->setFrameShape(QFrame::StyledPanel);
    controlbar->setStyleSheet(
        "#controlbar { background-color: rgba(30, 30, 30, 200); border-radius: 6px; }"
        "QPushButton, QToolButton { color: #FFFFFF; background: transparent; border: none; padding: 4px 8px; }"
        "QPushButton:hover, QToolButton:hover { background-color: rgba(255, 255, 255, 40); border-radius: 4px; }"
        "QLabel { color: #FFFFFF; background: transparent; }"
        "QSlider::groove:horizontal { height: 6px; background: #555555; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #4CAF50; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #FFFFFF; width: 14px; height: 14px; margin: -4px 0; border-radius: 7px; }"
    );
    controlbar->raise(); // 保持在最顶层

    QHBoxLayout* bar_layout = new QHBoxLayout(controlbar);
    bar_layout->setContentsMargins(10, 4, 10, 4);
    bar_layout->setSpacing(8);

    // 播放/暂停按钮
    QPushButton* playpause_btn = new QPushButton(controlbar);
    playpause_btn->setObjectName("playpause_pushButton");
    playpause_btn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    playpause_btn->setFixedSize(36, 30);
    bar_layout->addWidget(playpause_btn);

    // 当前播放时间标签
    QLabel* current_label = new QLabel("00:00:00", controlbar);
    current_label->setObjectName("current_time_label");
    current_label->setMinimumWidth(70);
    current_label->setAlignment(Qt::AlignCenter);
    bar_layout->addWidget(current_label);

    // 进度条
    QSlider* seek_slider = new QSlider(Qt::Horizontal, controlbar);
    seek_slider->setObjectName("seek_slider");
    seek_slider->setRange(0, 1000); // 内部使用千分比，避免整数毫秒精度丢失导致跳动
    seek_slider->setValue(0);
    bar_layout->addWidget(seek_slider, 1);

    // 总时长标签
    QLabel* total_label = new QLabel("00:00:00", controlbar);
    total_label->setObjectName("total_time_label");
    total_label->setMinimumWidth(70);
    total_label->setAlignment(Qt::AlignCenter);
    bar_layout->addWidget(total_label);

    // 静音按钮
    QToolButton* mute_btn = new QToolButton(controlbar);
    mute_btn->setObjectName("mute_toolButton");
    mute_btn->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));
    mute_btn->setCheckable(true);
    bar_layout->addWidget(mute_btn);

    // 音量滑块
    QSlider* volume_slider = new QSlider(Qt::Horizontal, controlbar);
    volume_slider->setObjectName("volume_slider");
    volume_slider->setRange(0, 100);
    volume_slider->setValue(last_volume);
    volume_slider->setFixedWidth(90);
    bar_layout->addWidget(volume_slider);

    // 将控制条停靠在视频区域底部
    controlbar->resize(ui->video_area->width() - 20, 46);
    controlbar->move(10, ui->video_area->height() - 56);
    controlbar->show();

    // ======== 悬浮控制条自动隐藏定时器 ========
    hide_timer = new QTimer(this);
    hide_timer->setSingleShot(true);
    hide_timer->setInterval(3000); // 3秒无鼠标移动后隐藏控制条
    connect(hide_timer, &QTimer::timeout, this, [controlbar]() {
        controlbar->hide();
    });
    controlbar->show(); // 初始显示

    // ======== 控件信号槽连接 ========
    connect(playpause_btn, &QPushButton::clicked, this, &PlayerDialog::on_playpause_pushButton_clicked);
    connect(seek_slider, &QSlider::sliderPressed, this, [this]() { user_seeking = true; });
    connect(seek_slider, &QSlider::sliderReleased, this, &PlayerDialog::on_seek_sliderReleased);
    connect(volume_slider, &QSlider::sliderMoved, this, &PlayerDialog::on_volume_sliderMoved);
    connect(mute_btn, &QToolButton::clicked, this, &PlayerDialog::on_mute_toolButton_clicked);

    // ======== PlayCore 事件信号槽连接（观察者模式 + 跨线程队列投递） ========
    connect(playcore, &PlayCore::sig_time_changed, this, &PlayerDialog::slot_time_changed, Qt::QueuedConnection);
    connect(playcore, &PlayCore::sig_length_changed, this, &PlayerDialog::slot_length_changed, Qt::QueuedConnection);
    connect(playcore, &PlayCore::sig_position_changed, this, &PlayerDialog::slot_position_changed, Qt::QueuedConnection);
    connect(playcore, &PlayCore::sig_state_changed, this, &PlayerDialog::slot_state_changed, Qt::QueuedConnection);

    // 恢复音量初始值
    playcore->set_volume(last_volume);
}

PlayerDialog::~PlayerDialog()
{
    stop_play();
    delete ui;
}

// 由外部调用：初始化播放器并加载RTSP媒体开始拉流
bool PlayerDialog::start_play(const QString& rtsp_url)
{
    // 确保渲染窗口已创建原生句柄
    ui->video_area->winId(); // 触发原生窗口创建

    if (!playcore->init_player(static_cast<quintptr>(ui->video_area->winId()))) {
        QMessageBox::critical(this, "播放器初始化", "播放引擎初始化失败，请检查libVLC运行库是否已正确部署。");
        return false;
    }
    if (!playcore->load_media(rtsp_url)) {
        QMessageBox::warning(this, "加载媒体", QString("无法加载媒体地址：\n%1").arg(rtsp_url));
        return false;
    }

    playcore->play();
    wake_controlbar(); // 唤醒控制条
    return true;
}

// 停止播放并释放播放器资源
void PlayerDialog::stop_play()
{
    if (playcore)
        playcore->stop();
}

// 鼠标移动事件：唤醒悬浮控制条
void PlayerDialog::mouseMoveEvent(QMouseEvent* event)
{
    wake_controlbar();
    QDialog::mouseMoveEvent(event);
}

// 关闭窗口事件：停止播放
void PlayerDialog::closeEvent(QCloseEvent* event)
{
    stop_play();
    QDialog::closeEvent(event);
}

// 唤醒并显示悬浮控制条，重置自动隐藏定时器
void PlayerDialog::wake_controlbar()
{
    QFrame* controlbar = ui->video_area->findChild<QFrame*>("controlbar");
    if (controlbar) {
        controlbar->show();
        controlbar->raise();
    }
    if (hide_timer)
        hide_timer->start();
}

// ======================== UI 控件事件槽 ========================

void PlayerDialog::on_playpause_pushButton_clicked()
{
    if (!playcore) return;
    QPushButton* btn = findChild<QPushButton*>("playpause_pushButton");
    // 依据当前状态判断是播放还是暂停（简单通过状态枚举判断）
    // 这里简化处理：如果正在播放则暂停，否则播放
    // 更严谨的做法是根据 slot_state_changed 缓存的最新状态判断
    static bool is_playing = false;
    if (!is_playing) {
        playcore->play();
        if (btn) btn->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        is_playing = true;
    } else {
        playcore->pause();
        if (btn) btn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        is_playing = false;
    }
    wake_controlbar();
}

void PlayerDialog::on_seek_sliderReleased()
{
    if (!playcore) return;
    QSlider* slider = findChild<QSlider*>("seek_slider");
    if (slider) {
        qint64 total = playcore->get_length();
        qint64 target = static_cast<qint64>(slider->value() / 1000.0 * total);
        playcore->set_time(target); // 触发libVLC内部发送带Range的PLAY RTSP请求实现跳转
    }
    user_seeking = false; // 恢复接受自动位置更新
    wake_controlbar();
}

void PlayerDialog::on_volume_sliderMoved(int value)
{
    if (!playcore) return;
    playcore->set_volume(value);
    muted = (value == 0);
    QToolButton* mute_btn = findChild<QToolButton*>("mute_toolButton");
    if (mute_btn) mute_btn->setChecked(muted);
    wake_controlbar();
}

void PlayerDialog::on_mute_toolButton_clicked()
{
    if (!playcore) return;
    muted = !muted;
    playcore->set_mute(muted);
    QToolButton* mute_btn = findChild<QToolButton*>("mute_toolButton");
    QSlider* volume_slider = findChild<QSlider*>("volume_slider");
    if (muted) {
        if (volume_slider) { last_volume = volume_slider->value(); volume_slider->setValue(0); }
        if (mute_btn) mute_btn->setIcon(style()->standardIcon(QStyle::SP_MediaVolumeMuted));
    } else {
        if (volume_slider) volume_slider->setValue(last_volume > 0 ? last_volume : 80);
        if (mute_btn) mute_btn->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));
        if (playcore) playcore->set_volume(last_volume > 0 ? last_volume : 80);
    }
    wake_controlbar();
}

void PlayerDialog::slot_hide_controlbar_timeout()
{
    // 已由wake_controlbar中的lambda直接处理，此槽保留以兼容未使用lambda场景
}

// ======================== PlayCore 事件信号槽 ========================

void PlayerDialog::slot_time_changed(qint64 current_ms)
{
    QLabel* label = findChild<QLabel*>("current_time_label");
    if (label) label->setText(format_time(current_ms));

    // 更新进度条位置（用户拖拽时不覆盖）
    if (!user_seeking) {
        QSlider* slider = findChild<QSlider*>("seek_slider");
        qint64 total = playcore ? playcore->get_length() : 0;
        if (slider && total > 0) {
            slider->blockSignals(true);
            slider->setValue(static_cast<int>(current_ms * 1000.0 / total));
            slider->blockSignals(false);
        }
    }
}

void PlayerDialog::slot_length_changed(qint64 total_ms)
{
    QLabel* label = findChild<QLabel*>("total_time_label");
    if (label) label->setText(format_time(total_ms));
}

void PlayerDialog::slot_position_changed(float pos)
{
    if (user_seeking) return;
    QSlider* slider = findChild<QSlider*>("seek_slider");
    if (slider && pos >= 0.0f && pos <= 1.0f) {
        slider->blockSignals(true);
        slider->setValue(static_cast<int>(pos * 1000.0f));
        slider->blockSignals(false);
    }
}

void PlayerDialog::slot_state_changed(int state)
{
    QPushButton* btn = findChild<QPushButton*>("playpause_pushButton");
    PlayerStatus status = static_cast<PlayerStatus>(state);
    switch (status) {
    case PlayerStatus::STATUS_PLAYING:
        if (btn) btn->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        break;
    case PlayerStatus::STATUS_PAUSED:
    case PlayerStatus::STATUS_STOPPED:
        if (btn) btn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        break;
    case PlayerStatus::STATUS_ENDED:
        if (btn) btn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        break;
    case PlayerStatus::STATUS_ERROR:
        QMessageBox::warning(this, "播放错误", "流媒体播放出现错误，请检查网络连接或服务器流媒体服务状态。");
        break;
    default:
        break;
    }
}

// 格式化毫秒为 hh:mm:ss
QString PlayerDialog::format_time(qint64 ms) const
{
    qint64 total_sec = ms / 1000;
    qint64 h = total_sec / 3600;
    qint64 m = (total_sec % 3600) / 60;
    qint64 s = total_sec % 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}
