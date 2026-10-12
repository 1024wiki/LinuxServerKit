#include "remotecontroldialog.h"
#include "ui_remotecontroldialog.h"
#include "tcpmgr.h"

#include <QtGlobal>
#include <QFileIconProvider>
#include <QIcon>

RemoteControlDialog::RemoteControlDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RemoteControlDialog)
    , server_ip()
    , rtsp_port(SERVER_RTSP_PORT)
{
    ui->setupUi(this);

    // 信号槽连接
    // 返回登录主界面槽函数(断开连接后返回)
    connect(ui->returnmian_pushButton,&QPushButton::clicked,this,[this](){
        // 返回前先断开与服务器的连接
        slot_tcp_disconnectserver();
        emit sig_switch_main();
    });
    // 断开连接服务器槽函数(返回主界面时断开连接)
    connect(this,&RemoteControlDialog::sig_tcp_disconnection,TcpMgr::get_instance().get(),&TcpMgr::slot_tcp_disconnectserver);
    // 连接服务器状态槽函数
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_tcp_connection_status,this,&RemoteControlDialog::slot_tcp_connection_status);

    // 请求获取磁盘分区信息槽函数(登录成功打开本界面后自动调用一次，以及点击刷新按钮时调用)
    connect(ui->file_pushButton,&QPushButton::clicked,this,&RemoteControlDialog::slot_getserver_driverinfo);
    // 处理服务器发送的磁盘分区信息在相应控件进行数据显示的槽函数
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_tip_notice_driverinfo,this,&RemoteControlDialog::slot_dealserver_driverinfo);

    // 连接dirtreeview的删除所有文件信号或刷新获取驱动目录信息和filetreeview的删除所有文件
    connect(this,&RemoteControlDialog::sig_deleteallfileinfo,ui->file_treeView,&FileTreeView::slot_deletefileinfo_allitem);
    connect(ui->dir_treeView,&DirTreeView::sig_notice_remvfile,ui->file_treeView,&FileTreeView::slot_deletefileinfo_allitem);

    watchwidget = new RemoteWatchWidget; // 远程监控显示窗口
    //watchwidget->setWindowFlags(watchwidget->windowFlags() | Qt::Window); // 设置该窗口不是子部件，而是完整窗口进行显示
    watchwork = new RemoteWatchWork; // 远程监控工作对象
    // 点击远程监控按钮，发出加入tcp推送组信号，在加入成功后，打开监视窗口，开启工作对象工作
    connect(ui->view_pushButton,&QPushButton::clicked,watchwork,&RemoteWatchWork::slot_joinremotewatch_request);
    // 加入成功或失败后，发出信号到主线程进行打开监控窗口或提示失败
    connect(watchwork,&RemoteWatchWork::sig_open_remotewatch_widget,this,&RemoteControlDialog::slot_open_remotewatch_widget);
    // 连接监视工作对象传出的屏幕截图数据进行显示在监视窗口中
    connect(watchwork,&RemoteWatchWork::sig_notice_screenpix,watchwidget,&RemoteWatchWidget::slot_show_screenpix);
    // 关闭远程监控窗口时，发出退出tcp推送组信号，并停止工作对象工作
    connect(watchwidget,&RemoteWatchWidget::sig_window_close,watchwork,&RemoteWatchWork::slot_exitremotewatch_request);

    // 连接锁机按钮和远程锁机命令
    connect(ui->lock_pushButton,&QPushButton::clicked,this,&RemoteControlDialog::slot_lock_remotehost);
    // 连接解锁按钮和远程解锁命令
    connect(ui->unlock_pushButton,&QPushButton::clicked,this,&RemoteControlDialog::slot_unlock_remotehost);

    // 初始化启动监视工作子线程
    watchthread = new QThread(this);
    watchwork->moveToThread(watchthread); // 将监视工作放入到子线程中完成
    watchthread->start();
}


RemoteControlDialog::~RemoteControlDialog()
{
    // 判断监控子窗口是否关闭，未关闭时先关闭窗口，发出退出tcp推送组信号
    if(watchwidget->isVisible())
        watchwidget->close();

    // 先阻塞回收子线程
    if(watchthread->isRunning()){
        watchthread->quit();
        watchthread->wait();
        watchthread->deleteLater();
    }
    // 释放工作对象资源
    watchwork->deleteLater();
    watchwidget->deleteLater();

    // 释放UI界面对象
    delete ui;
}


// 设置服务器IP和RTSP流媒体端口(由登录主界面在登录成功后调用)
void RemoteControlDialog::set_server_info(const QString& ip, unsigned short rtsp){
    server_ip = ip;
    rtsp_port = rtsp;
    qDebug()<<"信令控制界面接收到服务器信息：IP="<<server_ip<<" RTSP端口="<<rtsp_port;

    // 同步RTSP端口给文件树模块，用于右键文件播放时构造rtsp://拉流URL
    ui->file_treeView->set_rtsp_port(rtsp_port);

    // 登录成功后自动请求一次磁盘分区信息，初始化目录浏览区
    slot_getserver_driverinfo();
}

// 断开连接服务器槽函数(退出登录/返回主界面时调用)
void RemoteControlDialog::slot_tcp_disconnectserver(){
    // 未连接状态直接返回
    if(TcpMgr::get_instance()->get_connectstatus()==0){
        return;
    }
    emit sig_tcp_disconnection(); // 发送断开连接信号
}

// 连接服务器状态提示
void RemoteControlDialog::slot_tcp_connection_status(QString ip, unsigned short port, ServerConnectStatus status){
    if(status == ServerConnectStatus::STATUS_DISCONNECTED)
        qDebug() << QString("与服务器(%1:%2)断开连接！").arg(ip).arg(port);
    else if(status == ServerConnectStatus::STATUS_CONNECT_ERROR)
        qDebug() << QString("与服务器(%1:%2)连接出现错误").arg(ip).arg(port);
}


// 获取被控端服务器磁盘分区信息
void RemoteControlDialog::slot_getserver_driverinfo()
{
    // 通知删除文件列表的所有文件
    emit sig_deleteallfileinfo();

    // 构建json数据发送给服务器，然后通过信号方式进行发送
    QJsonObject jsonobj; // 保存发送数据作为json数据
    jsonobj["data"] = "empty";
    QJsonDocument jsondoc(jsonobj);
    QByteArray json=jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::GET_DRIVERINFO, json);
    qDebug()<<"发送的请求服务器磁盘分区信息："<<json;
}

// 处理被控端服务器发送的磁盘分区信息在相应控件进行显示
void RemoteControlDialog::slot_dealserver_driverinfo(QString system, QString driverinfo){
    qDebug()<<"成功接收的"<<system<<"服务器分区信息："<<driverinfo;
    QStringList drivers = driverinfo.split(",");
    // 避免重复添加，先删除所有存在的驱动器名
    ui->dir_treeView->delete_dirrow(0);
    // 在不可见顶级根目录下插入该驱动器名
    QFileIconProvider iconProvider;
    QIcon driveIcon = iconProvider.icon(QFileIconProvider::Drive);
    for(const QString& driver:drivers)
        ui->dir_treeView->insert_dirrow({driver},driveIcon); // 插入一个驱动器，Windows为CDE盘，Linux为根目录/
}

// 连接加入远程监控成功，并打开远程监控窗口信号(方案B：成功则拉取桌面RTSP流，无桌面则友好弹窗提示)
void RemoteControlDialog::slot_open_remotewatch_widget(RemoteWatchStatus status, QString msg){
    if(status == RemoteWatchStatus::WATCH_SUCCESS){
        // 构造桌面RTSP推流URL：rtsp://<server_ip>:<rtsp_port>/desktop
        QString desktop_url = QString("rtsp://%1:%2/desktop")
                                  .arg(server_ip.isEmpty() ? TcpMgr::get_instance()->get_ipstr() : server_ip)
                                  .arg(rtsp_port > 0 ? rtsp_port : SERVER_RTSP_PORT);

        qDebug()<<"开启方案B远程低延迟桌面监控，拉流URL:"<<desktop_url;

        if(!watchwidget->isVisible()){
            watchwidget->show();
            watchwidget->raise();
            watchwidget->activateWindow();
        }

        // 调用方案B接口启动低延迟桌面拉流
        watchwidget->start_desktop_stream(desktop_url);
    }
    else if(status == RemoteWatchStatus::WATCH_NO_DESKTOP){
        // 服务端无桌面环境(如 Ubuntu Server Headless，DISPLAY为空)，按需求友好提示
        QMessageBox::warning(this, "远程桌面监控",
            "【无法使用远程监控】\n"
            "远程服务器当前处于「无桌面环境」（Headless Server，未运行 X11/Wayland 桌面图形服务）。\n"
            "该功能仅在服务器已启动桌面时可用。文件管理与音视频点播功能仍可正常使用。");
    }
    else if(status == RemoteWatchStatus::WATCH_ENCODER_ERROR){
        QMessageBox::warning(this, "远程桌面监控",
            QString("远程监控启动失败：服务端H.264实时屏幕编码器未能正常启动。\n详情：%1").arg(msg));
    }
    else{
        QMessageBox::warning(this, "远程桌面监控",
            QString("加入远程监控失败！\n%1").arg(msg.isEmpty() ? "服务器内部错误" : msg));
    }
}

// 连接远程锁机按钮，发出远程主机进行锁机命令
void RemoteControlDialog::slot_lock_remotehost(){
    // 构建json数据发送给服务器，然后通过信号方式进行发送
    QJsonObject jsonobj; // 保存发送数据作为json数据
    jsonobj["data"] = "empty";
    QJsonDocument jsondoc(jsonobj);
    QByteArray json=jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::LOCK_REMOTE_CONTROL, json);
    qDebug()<<"发送的锁定远程服务器命令的json数据："<<json;
}

// 连接远程解锁按钮，发出远程主机进行解锁命令
void RemoteControlDialog::slot_unlock_remotehost(){
    // 构建json数据发送给服务器，然后通过信号方式进行发送
    QJsonObject jsonobj; // 保存发送数据作为json数据
    jsonobj["data"] = "empty";
    QJsonDocument jsondoc(jsonobj);
    QByteArray json=jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::UNLOCK_REMOTE_CONTROL, json);
    qDebug()<<"发送的解锁远程服务器命令的json数据："<<json;
}

