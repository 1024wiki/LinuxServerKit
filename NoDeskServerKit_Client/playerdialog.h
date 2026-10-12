#ifndef PLAYERDIALOG_H
#define PLAYERDIALOG_H

#include <QDialog>
#include <QTimer>
#include <QMouseEvent>
#include "playcore.h"

namespace Ui {
class PlayerDialog;
}

// PlayerDialog：视频点播播放窗口（View）
// 包含视频渲染区域(QWidget + HWND句柄直传PlayCore)、悬浮播放控制条(浮动QWidget容器)，
// 集成播放/暂停、进度条跳转、当前/总时长显示、静音与音量调整功能。
// 控制条在鼠标移动时唤醒并显示，静止一段时间后自动淡出隐藏。
class PlayerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PlayerDialog(QWidget *parent = nullptr);
    ~PlayerDialog();

    // 由外部(如FileTreeView)调用：传入RTSP URL，初始化PlayCore并开始拉流播放
    bool start_play(const QString& rtsp_url);

    // 由外部调用：停止播放并释放播放器资源（在窗口关闭时也会调用）
    void stop_play();

protected:
    // 重写鼠标移动事件，唤醒悬浮控制条并重置隐藏定时器
    void mouseMoveEvent(QMouseEvent* event) override;
    // 重写关闭窗口事件，停止播放并释放资源
    void closeEvent(QCloseEvent* event) override;

private slots:
    // ======== UI 控件事件槽 ========
    // 播放/暂停按钮点击
    void on_playpause_pushButton_clicked();
    // 进度条拖拽或点击跳转结束
    void on_seek_sliderReleased();
    // 音量滑块改变
    void on_volume_sliderMoved(int value);
    // 静音按钮切换
    void on_mute_toolButton_clicked();
    // 悬浮控制条隐藏定时器超时
    void slot_hide_controlbar_timeout();

    // ======== PlayCore 事件信号槽（观察者模式：UI控件为观察者） ========
    void slot_time_changed(qint64 current_ms);
    void slot_length_changed(qint64 total_ms);
    void slot_position_changed(float pos);
    void slot_state_changed(int state);

private:
    Ui::PlayerDialog *ui;
    PlayCore* playcore;              // 播放引擎（Model）
    QTimer* hide_timer;              // 悬浮控制条自动隐藏定时器
    bool user_seeking;               // 用户正在拖拽进度条标记，防止拖拽时被位置更新覆盖
    bool muted;                      // 当前静音状态
    int last_volume;                 // 静音前的音量记忆

    // 格式化毫秒为 hh:mm:ss 字符串
    QString format_time(qint64 ms) const;

    // 唤醒并显示悬浮控制条，重置自动隐藏定时器
    void wake_controlbar();
};

#endif // PLAYERDIALOG_H
