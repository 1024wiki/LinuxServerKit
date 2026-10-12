#ifndef MAINDIALOG_H
#define MAINDIALOG_H

#include <QDialog>
#include "global.h"

namespace Ui {
class MainDialog;
}

// MainDialog：登录主窗口(最先显示的界面)
// 负责输入远程主机IP、信令端口、流媒体RTSP端口、用户名、密码，
// 点击"远程登录"后发起TCP连接并发送登录请求，
// 接收到信令服务器登录成功响应后，发出sig_login_success信号打开信令控制界面。
class MainDialog : public QDialog
{
    Q_OBJECT
    // friend class MainWindow;

public:
    explicit MainDialog(QWidget *parent = nullptr);
    ~MainDialog();

private:
    Ui::MainDialog *ui;
    // 当前是否处于已登录状态(用于退出登录按钮判断)
    bool logined;
    // 点击远程登录按钮后暂存的信令服务器IP和信令端口
    QString pending_ip;
    unsigned short pending_port;
    // 提示尚未登录
    void tip_not_logined();

public slots:
    // 点击"远程登录"按钮：校验输入并发起TCP连接
    void slot_login_request();
    // TCP连接状态变化槽函数(连接成功后发送登录请求)
    void slot_tcp_connection_status(QString, unsigned short, ServerConnectStatus);
    // 接收信令服务器登录结果，成功则发出打开信令控制界面信号
    void slot_login_result(LoginStatus, QString);
    // 点击"退出登录"按钮：断开TCP连接并复位登录状态
    void slot_logout_request();

signals:
    // 登录成功后打开信令控制界面信号(携带服务器IP用于构造RTSP URL、RTSP端口)
    void sig_login_success(QString server_ip, unsigned short rtsp_port);
    // 登录请求需要建立TCP连接的信号(携带IP和信令端口)
    void sig_tcp_connection(QString, unsigned short);
    // 登录/退出需要断开TCP连接的信号
    void sig_tcp_disconnection();
    //
};

#endif // MAINDIALOG_H
