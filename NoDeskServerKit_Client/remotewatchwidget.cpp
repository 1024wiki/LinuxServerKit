#include "remotewatchwidget.h"
#include "ui_remotewatchwidget.h"
#include <QEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QJsonArray>
#include "tcpmgr.h"

RemoteWatchWidget::RemoteWatchWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RemoteWatchWidget)
    , playcore(nullptr)
    , stream_started(false)
{
    ui->setupUi(this);

    // 确保显示部件具有原生窗口句柄(供libVLC绑定HWND直出渲染)
    ui->showlabel->setAttribute(Qt::WA_NativeWindow, true);

    // 设置窗口与显示控件均可接收键盘焦点，确保键盘事件能被捕获
    this->setFocusPolicy(Qt::StrongFocus);
    ui->showlabel->setFocusPolicy(Qt::StrongFocus);

    // 给显示控件安装事件过滤器捕获鼠标与键盘事件
    ui->showlabel->installEventFilter(this);

    // 初始化播放引擎(方案B：复用PlayCore进行RTSP桌面拉流)
    playcore = new PlayCore(this);
}

RemoteWatchWidget::~RemoteWatchWidget()
{
    stop_desktop_stream();
    delete ui;
}

// 方案B：启动低延迟桌面RTSP拉流(传入rtsp://ip:port/desktop地址)
bool RemoteWatchWidget::start_desktop_stream(const QString& rtsp_url){
    if(!playcore){
        qWarning()<<"RemoteWatchWidget: playcore为空，无法启动桌面拉流。";
        return false;
    }

    // 确保原生窗口句柄已创建
    ui->showlabel->winId();

    if(!playcore->init_player(static_cast<quintptr>(ui->showlabel->winId()))){
        qWarning()<<"RemoteWatchWidget: 播放引擎绑定HWND失败。";
        return false;
    }

    if(!playcore->load_media(rtsp_url)){
        qWarning()<<"RemoteWatchWidget: 加载桌面流URL失败:"<<rtsp_url;
        return false;
    }

    playcore->play();
    stream_started = true;
    qDebug()<<"RemoteWatchWidget: 方案B低延迟桌面RTSP拉流已启动:"<<rtsp_url;
    return true;
}

// 方案B：停止桌面拉流
void RemoteWatchWidget::stop_desktop_stream(){
    if(playcore && stream_started){
        playcore->stop();
        stream_started = false;
        qDebug()<<"RemoteWatchWidget: 桌面RTSP拉流已停止。";
    }
}

// 用于接收外部屏幕截图数据信号进行显示(方案A兼容留存)
void RemoteWatchWidget::slot_show_screenpix(QPixmap screenpix){
    // 若已启用方案B的RTSP硬解渲染，则忽略传统图片推送
    if(stream_started)
        return;

    // 窗口不可见或最小化不进行显示
    if (!this->isVisible()||(this->windowState()& Qt::WindowMinimized))
        return;

    // 缩放显示
    ui->showlabel->setPixmap(screenpix.scaled(
        ui->showlabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    ));
}

// 重写关闭窗口事件，发出自定义的关闭窗口信号
void RemoteWatchWidget::closeEvent(QCloseEvent *event) {
    // 关闭时自动停止桌面拉流
    stop_desktop_stream();
    // 发出关闭窗口信号
    emit sig_window_close();
    // 调用父窗口事件处理器
    QWidget::closeEvent(event);
}

// 重写事件过滤器，过滤捕获鼠标事件，然后转化为json数据发送给服务器
bool RemoteWatchWidget::eventFilter(QObject *watched, QEvent *event) {
    // 不为在显示监控画面的鼠标事件不考虑
    if (watched != ui->showlabel)
        return QWidget::eventFilter(watched, event); // 执行父类的事件过滤器处理
    // 过滤鼠标事件
    switch (event->type()) {
    // 鼠标按下事件
    case QEvent::MouseButtonPress:
        captureMousePress(static_cast<QMouseEvent*>(event));
        break;
    // 鼠标释放事件
    case QEvent::MouseButtonRelease:
        captureMouseRelease(static_cast<QMouseEvent*>(event));
        break;
    // 鼠标移动事件
    case QEvent::MouseMove:
        captureMouseMove(static_cast<QMouseEvent*>(event));
        break;
    // 鼠标双击事件
    case QEvent::MouseButtonDblClick:
        captureMouseDoubleClick(static_cast<QMouseEvent*>(event));
        break;
    // 鼠标滚轮事件
    case QEvent::Wheel:
        captureWheelEvent(static_cast<QWheelEvent*>(event));
        break;
    // 键盘按下事件
    case QEvent::KeyPress:
        captureKeyPress(static_cast<QKeyEvent*>(event));
        break;
    // 键盘释放事件
    case QEvent::KeyRelease:
        captureKeyRelease(static_cast<QKeyEvent*>(event));
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event); // 执行父类的事件过滤器处理
}

// 鼠标按下事件处理
void RemoteWatchWidget::captureMousePress(QMouseEvent *event)
{
    // 捕获远程需要的鼠标事件属性，构造json数据发送给远程服务器执行
    QJsonObject jsonobj;
    // 记录位置相对比例
    int x = event->position().x();
    int y = event->position().y();
    jsonobj["xScale"] = static_cast<double>(x)/static_cast<double>(this->width());
    jsonobj["yScale"] = static_cast<double>(y)/static_cast<double>(this->height());
    // 记录鼠标按键
    jsonobj["button"] = static_cast<int>(event->button()); // 无(0)、左(1)、中(4)、右(2)
    // 记录鼠标事件
    jsonobj["type"] = "pressed"; // 标记为鼠标按下

    // 序列化后进行发送
    QJsonDocument jsondoc(jsonobj);
    QByteArray json=jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::MOUSE_REMOTE_CONTROL, json);
    qDebug()<<"发送的请求鼠标按下控制信息："<<json;

    // // 如果是左键按下，标记拖动开始
    // if (event->button() == Qt::LeftButton)
    //     dragstart = event->pos();
}

// 鼠标释放事件处理
void RemoteWatchWidget::captureMouseRelease(QMouseEvent *event){
    // 捕获远程需要的鼠标事件属性，构造json数据发送给远程服务器执行
    QJsonObject jsonobj;
    // 记录位置相对比例
    int x = event->position().x();
    int y = event->position().y();
    jsonobj["xScale"] = static_cast<double>(x)/static_cast<double>(this->width());
    jsonobj["yScale"] = static_cast<double>(y)/static_cast<double>(this->height());
    // 记录鼠标按键
    jsonobj["button"] = static_cast<int>(event->button()); // 无(0)、左(1)、中(4)、右(2)
    // 记录鼠标事件
    jsonobj["type"] = "released"; // 标记为鼠标按下

    // 序列化后进行发送
    QJsonDocument jsondoc(jsonobj);
    QByteArray json=jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::MOUSE_REMOTE_CONTROL, json);
    qDebug()<<"发送的请求鼠标释放控制信息："<<json;
}

// 鼠标移动事件处理
void RemoteWatchWidget::captureMouseMove(QMouseEvent *event)
{
    // 记录上一次发送时间，防止发送过于频繁
    static QTime lastMoveTime = QTime::currentTime();
    int elapsed = lastMoveTime.msecsTo(QTime::currentTime());
    // 设置移动事件发送间隔，避免网络拥堵
    if (elapsed < 200)
        return;  // 跳过过于频繁的移动事件
    lastMoveTime = QTime::currentTime();

    QJsonObject jsonobj;
    // 记录起点位置和终点位置相对比例
    // // 起点位置
    // jsonobj["xScale_start"] = static_cast<double>(dragstart.x())/static_cast<double>(this->width());
    // jsonobj["yScale_start"] = static_cast<double>(dragstart.y())/static_cast<double>(this->height());
    // 终点位置
    int x = event->position().x();
    int y = event->position().y();
    jsonobj["xScale"] = static_cast<double>(x)/static_cast<double>(this->width());
    jsonobj["yScale"] = static_cast<double>(y)/static_cast<double>(this->height());

    // // 记录当前按下的所有按钮状态
    // Qt::MouseButtons buttons = event->buttons();
    // QJsonArray buttonsArray;
    // if (buttons & Qt::LeftButton) buttonsArray.append(1);
    // if (buttons & Qt::RightButton) buttonsArray.append(2);
    // if (buttons & Qt::MiddleButton) buttonsArray.append(4);
    // if (buttons & Qt::XButton1) buttonsArray.append(8);    // 后退键
    // if (buttons & Qt::XButton2) buttonsArray.append(16);   // 前进键
    // jsonobj["buttons"] = buttonsArray;

    // 标记为鼠标移动事件
    jsonobj["type"] = "moved";

    // 序列化后发送
    QJsonDocument jsondoc(jsonobj);
    QByteArray json = jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::MOUSE_REMOTE_CONTROL, json);

    // 调试信息
    static int moveCount = 0;
    // 每10次输出一次
    if (++moveCount % 10 == 0)
        qDebug() << "发送的鼠标移动控制信息：" << json;
}

// 鼠标双击事件处理
void RemoteWatchWidget::captureMouseDoubleClick(QMouseEvent *event)
{
    QJsonObject jsonobj;
    // 记录位置相对比例
    int x = event->position().x();
    int y = event->position().y();
    jsonobj["xScale"] = static_cast<double>(x)/static_cast<double>(this->width());
    jsonobj["yScale"] = static_cast<double>(y)/static_cast<double>(this->height());

    // 记录鼠标按键
    jsonobj["button"] = static_cast<int>(event->button());
    // 标记为鼠标双击事件
    jsonobj["type"] = "dblclick";

    // 序列化后发送
    QJsonDocument jsondoc(jsonobj);
    QByteArray json = jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::MOUSE_REMOTE_CONTROL, json);
    qDebug() << "发送的鼠标双击控制信息：" << json;
}

// 滚轮事件处理
void RemoteWatchWidget::captureWheelEvent(QWheelEvent *event)
{
    // 记录上一次发送时间，防止发送过于频繁
    static QTime lastMoveTime = QTime::currentTime();
    int elapsed = lastMoveTime.msecsTo(QTime::currentTime());
    // 设置移动事件发送间隔，避免网络拥堵
    if (elapsed < 500)
        return;  // 跳过过于频繁的滚轮事件
    lastMoveTime = QTime::currentTime();

    QJsonObject jsonobj;
    // 记录位置相对比例
    int x = event->position().x();
    int y = event->position().y();
    jsonobj["xScale"] = static_cast<double>(x)/static_cast<double>(this->width());
    jsonobj["yScale"] = static_cast<double>(y)/static_cast<double>(this->height());

    // 记录滚轮方向
    int delta = 0; // 记录垂直或水平滚动及相应的向上或向下滚动
    QPoint angleDelta = event->angleDelta();  // 角度变化
    if (abs(angleDelta.y()) > abs(angleDelta.x())) {
        // 垂直滚动
        delta = angleDelta.y() > 0 ? 4 : 5;  // X11中，4表示垂直向上，5表示垂直向下
    }
    else // 水平滚动
        delta = angleDelta.x() > 0 ? 6 : 7;  // X11中，6表示水平向上，7表示垂水平向下
    jsonobj["button"] = delta;

    // 标记为滚轮事件
    jsonobj["type"] = "wheel";

    // 序列化后发送
    QJsonDocument jsondoc(jsonobj);
    QByteArray json = jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::MOUSE_REMOTE_CONTROL, json);
    qDebug() << "发送的滚轮控制信息：" << json;
}

// 键盘按下事件处理
void RemoteWatchWidget::captureKeyPress(QKeyEvent *event)
{
    // 忽略自动重复触发的按键(长按时Windows/Linux通常产生auto-repeat)
    if (event->isAutoRepeat())
        return;

    QJsonObject jsonobj;
    // 记录Qt按键值(如 Qt::Key_A、Qt::Key_Return 等)与按下/释放状态
    jsonobj["keycode"] = static_cast<int>(event->key());
    // 记录修饰键状态(Ctrl/Shift/Alt/Ctrl)，便于服务端处理组合键(如 Ctrl+C)
    jsonobj["modifiers"] = static_cast<int>(event->modifiers());
    // 记录实际输入的字符(仅可打印字符有效，控制键为null)
    jsonobj["text"] = event->text();
    // 标记为键盘按下事件
    jsonobj["type"] = "pressed";

    // 序列化后发送
    QJsonDocument jsondoc(jsonobj);
    QByteArray json = jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::KEYBOARD_REMOTE_CONTROL, json);
    qDebug() << "发送的键盘按下控制信息：" << json;
}

// 键盘释放事件处理
void RemoteWatchWidget::captureKeyRelease(QKeyEvent *event)
{
    // 忽略自动重复触发的按键
    if (event->isAutoRepeat())
        return;

    QJsonObject jsonobj;
    // 记录Qt按键值与按下/释放状态
    jsonobj["keycode"] = static_cast<int>(event->key());
    // 记录修饰键状态
    jsonobj["modifiers"] = static_cast<int>(event->modifiers());
    // 记录实际输入的字符
    jsonobj["text"] = event->text();
    // 标记为键盘释放事件
    jsonobj["type"] = "released";

    // 序列化后发送
    QJsonDocument jsondoc(jsonobj);
    QByteArray json = jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::KEYBOARD_REMOTE_CONTROL, json);
    qDebug() << "发送的键盘释放控制信息：" << json;
}

