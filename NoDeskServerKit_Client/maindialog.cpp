#include "maindialog.h"
#include "ui_maindialog.h"
#include "tcpmgr.h"
#include <QDebug>
#include <QRegularExpression>

MainDialog::MainDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MainDialog)
    , logined(false)
    , pending_ip()
    , pending_port(SERVER_LISTEN_PORT)
{
    ui->setupUi(this);

    // 信号槽连接
    // "远程登录"按钮触发登录请求槽函数
    connect(this->ui->pushButton_remotecontrol,&QPushButton::clicked,this,&MainDialog::slot_login_request);
    // "退出登录"按钮触发退出登录槽函数
    connect(this->ui->disconnect_pushButton,&QPushButton::clicked,this,&MainDialog::slot_logout_request);

    // 登录TCP连接/断开信号与TcpMgr网络子线程槽函数连接(通过信号槽保证跨线程执行)
    connect(this,&MainDialog::sig_tcp_connection,TcpMgr::get_instance().get(),&TcpMgr::slot_tcp_connectserver);
    connect(this,&MainDialog::sig_tcp_disconnection,TcpMgr::get_instance().get(),&TcpMgr::slot_tcp_disconnectserver);

    // 接收TCP连接状态变化，连接成功后自动发送登录请求
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_tcp_connection_status,this,&MainDialog::slot_tcp_connection_status);
    // 接收信令服务器登录结果，成功则打开信令控制界面
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_login_result,this,&MainDialog::slot_login_result);

    qDebug()<<"登录主界面被创建了";
}

MainDialog::~MainDialog()
{
    delete ui;
    qDebug()<<"登录主界面被析构了";
}

// 点击"远程登录"按钮：校验输入并发起TCP连接
void MainDialog::slot_login_request(){
    // 已登录时直接提示
    if(logined){
        QMessageBox::information(this,"远程登录","已经登录服务器，请先退出登录！");
        return;
    }

    // 1. 校验服务器IP地址
    QString ipstr = ui->ip_lineEdit->text().trimmed();
    if(ipstr.isEmpty()){
        QMessageBox::critical(this,"服务器IP地址","输入的服务器IP地址为空!",QMessageBox::Ok);
        return;
    }
    else
    {
        static QRegularExpression regex("^((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])\\.){3}(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])$");
        if(!regex.match(ipstr).hasMatch()){
            QMessageBox::critical(this,"服务器IP地址",QString("输入的服务器IP地址:%1无效!").arg(ipstr),QMessageBox::Ok);
            return;
        }
    }

    // 2. 校验信令登录端口号
    QString portstr = ui->port_lineEdit->text().trimmed();
    if(portstr.isEmpty()){
        QMessageBox::critical(this,"登录端口","输入的登录端口为空!",QMessageBox::Ok);
        return;
    }
    else
    {
        static QRegularExpression regex("^([1-9]\\d{0,3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])$");
        if(!regex.match(portstr).hasMatch()){
            QMessageBox::critical(this,"登录端口",QString("输入的登录端口:%1无效!").arg(portstr),QMessageBox::Ok);
            return;
        }
    }
    unsigned short port = portstr.toUShort();

    // 3. 校验流媒体RTSP端口号(用于登录成功后构造rtsp://ip:port/...拉流URL)
    QString rtsp_port_str = ui->rtsp_port_lineEdit->text().trimmed();
    if(rtsp_port_str.isEmpty()){
        QMessageBox::critical(this,"流媒体端口","输入的流媒体端口为空!",QMessageBox::Ok);
        return;
    }
    else
    {
        static QRegularExpression regex_rtsp("^([1-9]\\d{0,3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])$");
        if(!regex_rtsp.match(rtsp_port_str).hasMatch()){
            QMessageBox::critical(this,"流媒体端口",QString("输入的流媒体端口:%1无效!").arg(rtsp_port_str),QMessageBox::Ok);
            return;
        }
    }

    // 4. 校验用户名和密码非空
    QString username = ui->ip_lineEdit_2->text().trimmed();
    if(username.isEmpty()){
        QMessageBox::critical(this,"用户名","输入的用户名为空!",QMessageBox::Ok);
        return;
    }
    QString password = ui->ip_lineEdit_3->text();
    if(password.isEmpty()){
        QMessageBox::critical(this,"密码","输入的密码为空!",QMessageBox::Ok);
        return;
    }

    // 确定登录连接
    int ret = QMessageBox::question(this, "远程登录", QString("是否登录服务器(%1:%2)?").arg(ipstr).arg(port),
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Ok
    );
    if(ret==QMessageBox::Ok){
        qDebug()<<"远程登录，服务器地址："<<ipstr<<":"<<port;
        // 暂存连接信息，待TCP连接成功后在slot_tcp_connection_status中发送登录请求
        pending_ip = ipstr;
        pending_port = port;
        emit sig_tcp_connection(ipstr, port); // 发射连接TCP服务器信号
    }
}

// TCP连接状态变化槽函数(连接成功后发送登录请求)
void MainDialog::slot_tcp_connection_status(QString ip, unsigned short port, ServerConnectStatus status){
    if(status == ServerConnectStatus::STATUS_CONNECTED){
        qDebug()<<"TCP连接成功，发送登录请求。用户名："<<ui->ip_lineEdit_2->text();
        // 构造携带用户名和密码的登录请求给信令服务器
        QJsonObject jsonobj;
        jsonobj["username"] = ui->ip_lineEdit_2->text().trimmed();
        jsonobj["password"] = ui->ip_lineEdit_3->text();
        QJsonDocument jsondoc(jsonobj);
        QByteArray json=jsondoc.toJson();
        emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::LOGIN_USER, json);
        qDebug()<<"发送的登录请求："<<json;
    }
    else if(status == ServerConnectStatus::STATUS_CONNECT_ERROR){
        QMessageBox::warning(this,"远程登录",QString("连接服务器(%1:%2)失败！").arg(ip).arg(port));
    }
    else if(status == ServerConnectStatus::STATUS_DISCONNECTED){
        // 登录状态下意外断开连接，复位登录状态
        if(logined){
            logined = false;
            ui->pushButton_remotecontrol->setEnabled(true);
            ui->disconnect_pushButton->setEnabled(false);
            QMessageBox::warning(this,"远程登录",QString("与服务器(%1:%2)的连接已断开！").arg(ip).arg(port));
        }
    }
}

// 接收信令服务器登录结果，成功则发出打开信令控制界面信号
void MainDialog::slot_login_result(LoginStatus status, QString username){
    if(status == LoginStatus::LOGIN_SUCCESS){
        logined = true;
        // 登录成功：读取RTSP端口并发出打开信令控制界面信号
        unsigned short rtsp_port = ui->rtsp_port_lineEdit->text().trimmed().toUShort();
        QMessageBox::information(this,"远程登录",QString("用户[%1]登录服务器成功！").arg(username));
        ui->pushButton_remotecontrol->setEnabled(false);
        ui->disconnect_pushButton->setEnabled(true);
        emit sig_login_success(pending_ip, rtsp_port);
    }
    else if(status == LoginStatus::LOGIN_USERNAME_ERROR){
        QMessageBox::warning(this,"远程登录",QString("登录失败：用户[%1]不存在！").arg(username));
        emit sig_tcp_disconnection(); // 登录失败断开连接
    }
    else if(status == LoginStatus::LOGIN_PASSWORD_ERROR){
        QMessageBox::warning(this,"远程登录",QString("登录失败：用户[%1]密码错误！").arg(username));
        emit sig_tcp_disconnection(); // 登录失败断开连接
    }
    else{
        QMessageBox::critical(this,"远程登录",QString("登录失败：服务器内部错误！"));
        emit sig_tcp_disconnection(); // 登录失败断开连接
    }
}

// 点击"退出登录"按钮：断开TCP连接并复位登录状态
void MainDialog::slot_logout_request(){
    // 未登录状态直接返回
    if(!logined){
        tip_not_logined();
        return;
    }

    // 确定退出登录
    int ret = QMessageBox::question(
        this, "退出登录", QString("是否退出登录服务器(%1:%2)?").arg(pending_ip).arg(pending_port),
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Ok
    );
    if(ret==QMessageBox::Ok){
        emit sig_tcp_disconnection(); // 发送断开连接信号
        // 复位登录状态
        logined = false;
        ui->pushButton_remotecontrol->setEnabled(true);
        ui->disconnect_pushButton->setEnabled(false);
        QMessageBox::information(this,"退出登录","已退出登录。");
    }
}

// 提示尚未登录
void MainDialog::tip_not_logined(){
    QMessageBox::information(this,"退出登录","尚未登录服务器！");
}
