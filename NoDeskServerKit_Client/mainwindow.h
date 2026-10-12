#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "maindialog.h"
#include "remotecontroldialog.h"
#include "global.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    ~MainWindow();
    // 获取静态单例对象
    static std::shared_ptr<MainWindow> get_instance();

public slots:
    void slot_switch_Main_Dialog();   // 打开登录主界面(最先显示)
    void slot_login_success(QString server_ip, unsigned short rtsp_port); // 登录成功后打开信令控制界面
    void slot_switch_remotecontrol_Dialog();  // 打开信令控制界面(登录成功后调用)

private:
    // 构造函数
    static std::shared_ptr<MainWindow> instance;
    MainWindow(QWidget *parent = nullptr);

    Ui::MainWindow *ui;
    // 总主界面
    MainDialog *_main_dialog;
    // 远程控制界面
    RemoteControlDialog *_remotecontrol_dialog;
    // 其他界面

};
#endif // MAINWINDOW_H
