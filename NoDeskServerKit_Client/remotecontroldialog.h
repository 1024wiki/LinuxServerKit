#ifndef REMOTECONTROLDIALOG_H
#define REMOTECONTROLDIALOG_H

#include <QDialog>
#include <QThread>
#include "global.h"
#include "remotewatchwork.h"
#include "remotewatchwidget.h"

namespace Ui {
class RemoteControlDialog;
}


// RemoteControlDialog：信令控制界面(登录成功后打开)
// 负责目录/文件浏览、文件上传下载删除执行、流媒体播放、远程监控锁机等功能，
// 登录连接信息(服务器IP/信令端口/RTSP端口)由登录主界面在登录成功后通过set_server_info传入。
class RemoteControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RemoteControlDialog(QWidget *parent = nullptr);
    ~RemoteControlDialog();

    // 获取工作子线程对象
    QThread* get_workThread() const;

    // 由登录主界面在登录成功后调用：设置服务器IP和RTSP流媒体端口(用于构造rtsp://拉流URL)
    void set_server_info(const QString& server_ip, unsigned short rtsp_port);

    // 断开连接服务器槽函数(退出登录/返回主界面时调用)
    void slot_tcp_disconnectserver();
    // 连接服务器状态提示槽函数
    void slot_tcp_connection_status(QString, unsigned short, ServerConnectStatus);

    // 请求被控端服务器磁盘分区信息
    void slot_getserver_driverinfo();
    // 处理被控端服务器发送的磁盘分区信息在相应控件进行显示
    void slot_dealserver_driverinfo(QString system, QString driverinfo);

    // 连接线程成功传出的加入远程监视，打开远程监控客户端(方案B：处理成功启动RTSP桌面流，或提示无桌面)
    void slot_open_remotewatch_widget(RemoteWatchStatus status, QString msg);

    // 连接远程锁机按钮，发出远程主机进行锁机命令
    void slot_lock_remotehost();

    // 连接远程解锁按钮，发出远程主机进行解锁命令
    void slot_unlock_remotehost();

private:
    Ui::RemoteControlDialog *ui;
    // 监视工作子线程
    QThread* watchthread;
    // 监视工作对象
    RemoteWatchWork* watchwork;
    // 显示监视屏幕窗口
    RemoteWatchWidget* watchwidget;
    // 登录成功的服务器IP和RTSP流媒体端口(由登录主界面传入)
    QString server_ip;
    unsigned short rtsp_port;

signals:
    // 断开连接tcp服务器信号(返回主界面时断开连接)
    void sig_tcp_disconnection();

    // 通知删除所有文件信息
    void sig_deleteallfileinfo();


    // 通知主窗口切换为主界面信号
    void sig_switch_main();
};

#endif // REMOTECONTROLDIALOG_H
