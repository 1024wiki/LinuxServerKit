#include "remotewatchwork.h"
#include "tcpmgr.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>

RemoteWatchWork::RemoteWatchWork(QObject *parent): QObject(parent), joinserver(false)
{
    // 设置显示超时定时器启动和超时时间间隔
    showTimer = new QTimer(this);

    // 连接信号槽
    // 连接加入服务器推送截图组成功后，开始工作
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_joinremotewatch_result,this,&RemoteWatchWork::slot_joinremotewatch_result);
    // 连接超时信号和发出截图缓存数据信号
    connect(showTimer,&QTimer::timeout,this,&RemoteWatchWork::slot_send_screenpix_signal);
}

RemoteWatchWork::~RemoteWatchWork()
{
    if(showTimer->isActive())
        showTimer->stop();
}

// 接收主窗口发出的加入tcp推送组请求信号
void RemoteWatchWork::slot_joinremotewatch_request(){
    // 标记为true时，在检查没有加入时，发出加入tcp推送组请求
    if(joinserver)
        return;
    // 构造json数据发送请求
    QJsonObject json;
    json["joinscreen_group"] = "";
    QJsonDocument jsondoc(json);
    QByteArray jsonstr = jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::JOIN_TCP_REMOTEWATCH, jsonstr);
    qDebug()<<"发出加入服务器推送流请求的json数据："<<jsonstr;
}

// 接收主窗口发出的断开加入tcp推送组信号
void RemoteWatchWork::slot_exitremotewatch_request(){

    qDebug()<<"发出退出服务器推送流请求。";
    // 本来就是未加入状态，直接返回
    if(!joinserver)
        return;
    // 停止工作
    unenable_work();
    // 发送不加入tcp推送组请求，todo
    joinserver=false; // 标记未加入状态
    // 构造json数据发送请求
    QJsonObject json;
    json["exitscreen_group"] = "";
    QJsonDocument jsondoc(json);
    QByteArray jsonstr = jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::EXIT_TCP_REMOTEWATCH, jsonstr);
    qDebug()<<"发出退出服务器推送流请求的json数据："<<jsonstr;

}

// 连接tcp加入服务器推送流结果标记
void RemoteWatchWork::slot_joinremotewatch_result(RemoteWatchStatus status, QString msg){
    // 加入服务器推送流成功后进行标记，并设置工作开启状态
    if(status == RemoteWatchStatus::WATCH_SUCCESS){
        // 发出主线程打开监视窗口信号
        emit sig_open_remotewatch_widget(status, msg);
        joinserver=true; // 加入推送流标记成功
        // 开启工作
        enable_workinit();
    }
    else {
        // 加入失败(无桌面环境/编码器错误等)，通知主线程
        joinserver=false;
        emit sig_open_remotewatch_widget(status, msg);
    }
}

// 初始化连接和启动状态
void RemoteWatchWork::enable_workinit(){
    // 仅仅只在加入服务器推送流后才进行工作
    if(joinserver){
        if(!showTimer->isActive())
            showTimer->start(20); // 设置超时显示时间为30毫秒
        // 连接接收tcp传出的屏幕截图数据
        connect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_remotewatch_screenpix,this,&RemoteWatchWork::slot_recvtcp_screenpix);
    }
}

// 初始化断开连接和未启动状态
void RemoteWatchWork::unenable_work(){
    // 停止超时计时器的工作
    if(showTimer->isActive())
        showTimer->stop();
    // 断开连接接收tcp传出的屏幕截图数据
    disconnect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_remotewatch_screenpix,this,&RemoteWatchWork::slot_recvtcp_screenpix);
}

// 用于连接显示超时信号，发成截图数据信号
void RemoteWatchWork::slot_send_screenpix_signal(){
    emit sig_notice_screenpix(screenPix); // 直接发出缓存的屏幕截图
}

// 用于接收tcp传出图片数据进行更新缓存数据
void RemoteWatchWork::slot_recvtcp_screenpix(QPixmap pix){
    screenPix = pix; // 直接更新缓存的服务器传出的pix截图
}
