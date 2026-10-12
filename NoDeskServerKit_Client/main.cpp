#include "mainwindow.h"
#include "remotecontroldialog.h"

#include <QApplication>
#include <QFile>
#include <QDebug>
#include <memory>

// 加载全局 QSS 样式表
static void load_style(QApplication& app)
{
    // 优先从 qrc 资源加载，若开发阶段未打包则尝试从本地文件加载
    QFile file(":/style.qss");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        file.setFileName("style.qss");
        file.open(QIODevice::ReadOnly | QIODevice::Text);
    }
    if (file.isOpen()) {
        QString style = QLatin1String(file.readAll());
        app.setStyleSheet(style);
        file.close();
        qDebug() << "全局深色现代科技风 QSS 样式表加载成功。";
    } else {
        qWarning() << "未找到 style.qss 样式表文件。";
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // 加载现代深色科技风主题
    load_style(a);
    // 获取静态单例主界面进行显示
    std::shared_ptr<MainWindow> mainwindow = MainWindow::get_instance();
    mainwindow->show();
    return a.exec();
}
