#ifndef REMOTEWATCHWIDGET_H
#define REMOTEWATCHWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QKeyEvent>
#include "playcore.h"

namespace Ui {
class RemoteWatchWidget;
}

// RemoteWatchWidget：远程桌面低延迟监控与控制窗口(方案B：RTSP流媒体拉流 + 键鼠指令注入)
// 渲染层：复用 libVLC 播放引擎(PlayCore)绑定原生 HWND 句柄，低延迟拉取 rtsp://ip:port/desktop 视频流；
// 控制层：事件过滤器捕获鼠标与键盘事件，通过信令通道 MOUSE(1009) / KEYBOARD(1015) 实时注入服务端。
class RemoteWatchWidget : public QWidget
{
    Q_OBJECT
private:
    PlayCore* playcore;     // 方案B：低延迟RTSP桌面拉流播放引擎
    bool stream_started;    // 是否已开始拉流

public:
    explicit RemoteWatchWidget(QWidget *parent = nullptr);
    ~RemoteWatchWidget();

    // 方案B：启动低延迟桌面RTSP拉流(传入rtsp://ip:port/desktop地址)
    bool start_desktop_stream(const QString& rtsp_url);
    // 方案B：停止桌面拉流
    void stop_desktop_stream();

    // 兼容方案A：用于接收外部屏幕截图数据信号进行显示(若服务端仍采用Base64图片推送)
    void slot_show_screenpix(QPixmap);


    // 重写事件过滤器，过滤捕获鼠标事件，然后转化为json数据发送给服务器
    bool eventFilter(QObject *watched, QEvent *event) override;

    // 鼠标事件
    // 鼠标按下事件处理
    void captureMousePress(QMouseEvent *event);
    // 鼠标释放事件处理
    void captureMouseRelease(QMouseEvent *event);
    // 鼠标移动事件处理
    void captureMouseMove(QMouseEvent *event);
    // 鼠标双击事件处理
    void captureMouseDoubleClick(QMouseEvent *event);
    // 滚轮事件处理
    void captureWheelEvent(QWheelEvent *event);
    // 键盘事件处理
    void captureKeyPress(QKeyEvent *event);
    // 键盘释放事件处理
    void captureKeyRelease(QKeyEvent *event);

protected:
    // 重写关闭窗口事件，发出自定义的关闭窗口信号
    void closeEvent(QCloseEvent *event) override;

signals:
    // 发出关闭窗口信号
    void sig_window_close();

private:
    Ui::RemoteWatchWidget *ui;
};

#endif // REMOTEWATCHWIDGET_H
