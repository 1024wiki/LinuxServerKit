#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QPushButton>
#include <mutex>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    // 初始化显示总主界面
    this->slot_switch_Main_Dialog();
}

// 获取静态单例对象
std::shared_ptr<MainWindow> MainWindow::instance = nullptr;
std::shared_ptr<MainWindow> MainWindow::get_instance(){
    //设置为静态std::once_flag变量flag_once只会初始化一次，使用call_once也只会调用一次回调函数初始化单例静态实例instance
    static std::once_flag flag_once;
    std::call_once(flag_once,[](){
        instance=std::shared_ptr<MainWindow>(new MainWindow);//只能使用构造函数
    });
    return instance;
}

// 打开登录主界面(程序启动最先显示)
void MainWindow::slot_switch_Main_Dialog(){
    // 登录主界面类成员初始化
    this->_main_dialog = new MainDialog(this);
    // 内嵌主窗口显示
    this->_main_dialog->setWindowFlags(Qt::CustomizeWindowHint|Qt::FramelessWindowHint);
    // 设置在主窗口显示
    this->setCentralWidget(this->_main_dialog);

    // 槽函数的链接
    // 登录主界面登录成功信号，打开信令控制界面
    connect(this->_main_dialog,&MainDialog::sig_login_success,this,&MainWindow::slot_login_success);
}

// 登录成功后打开信令控制界面(携带登录服务器IP和RTSP流媒体端口)
void MainWindow::slot_login_success(QString server_ip, unsigned short rtsp_port){
    this->slot_switch_remotecontrol_Dialog();
    // 将登录服务器IP和RTSP端口同步给信令控制界面，用于文件树模块构造rtsp://拉流URL
    if(this->_remotecontrol_dialog)
        this->_remotecontrol_dialog->set_server_info(server_ip, rtsp_port);
}

// 打开信令控制界面(登录成功后调用)
void MainWindow::slot_switch_remotecontrol_Dialog(){
    // 信令控制界面类成员初始化
    this->_remotecontrol_dialog = new RemoteControlDialog(this);
    // 内嵌主窗口显示
    this->_remotecontrol_dialog->setWindowFlags(Qt::CustomizeWindowHint|Qt::FramelessWindowHint);
    // 设置在主窗口显示
    this->setCentralWidget(this->_remotecontrol_dialog);

    // 槽函数的链接
    // 信令控制界面返回按钮，返回登录主界面
    connect(this->_remotecontrol_dialog,&RemoteControlDialog::sig_switch_main,this,&MainWindow::slot_switch_Main_Dialog);
}

MainWindow::~MainWindow()
{
    delete ui;
}
