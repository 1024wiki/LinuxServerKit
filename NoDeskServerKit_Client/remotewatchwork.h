#ifndef REMOTEWATCHWORK_H
#define REMOTEWATCHWORK_H

#include <QObject>
#include <QTimer>
#include <QPixmap>
#include "global.h"

class RemoteWatchWork: public QObject
{
    Q_OBJECT
private:
    QPixmap screenPix; // 存储服务器发送的截图数据
    QTimer* showTimer; // 显示定时器
    bool joinserver;   // 标记加入服务器推送流成功

    // 初始化连接和启动状态
    void enable_workinit();
    // 初始化断开连接和未启动状态
    void unenable_work();

public:
    RemoteWatchWork(QObject* parent = nullptr);
    ~RemoteWatchWork();

    // 接收主窗口发出的加入tcp推送组请求信号
    void slot_joinremotewatch_request();
    // 接收主窗口发出的断开加入tcp推送组信号
    void slot_exitremotewatch_request();
    // 连接tcp加入服务器推送流结果标记(方案B：传递RemoteWatchStatus与提示信息)
    void slot_joinremotewatch_result(RemoteWatchStatus status, QString msg);

    // 用于接收tcp传出图片数据进行更新缓存数据
    void slot_recvtcp_screenpix(QPixmap);
    // 用于连接显示超时信号，发成截图数据信号
    void slot_send_screenpix_signal();

signals:
    // 发出主线程打开监视窗口信号(方案B：传递状态与信息)
    void sig_open_remotewatch_widget(RemoteWatchStatus status, QString msg);
    // 用于传出截图数据信号进行外部显示
    void sig_notice_screenpix(QPixmap);
};

#endif // REMOTEWATCHWORK_H
