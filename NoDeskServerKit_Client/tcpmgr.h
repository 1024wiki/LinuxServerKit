#ifndef TCPMGR_H
#define TCPMGR_H

#include "global.h"
#include <QTcpSocket>
#include <QThread>
#include <QPixmap>
#include <QSemaphore>


//TcpMgr类：用于管理前端登录后，与ChatServer服务器建立的tcp长连接，包括建立tcp连接、发送数据、接收数据并黏包处理，对接收的数据进行逻辑处理
class TcpMgr: public QObject {
    Q_OBJECT
public:
    //析构函数
    ~TcpMgr();
    // 获取静态单例对象
    static std::shared_ptr<TcpMgr> get_instance();

    // tcp长连接服务器
    void slot_tcp_connectserver(QString ip, unsigned short port);
    // 用于tcp主动断开连接服务器，但会发送缓冲区最后的数据
    void slot_tcp_disconnectserver();

    // 获取连接的服务器IP地址
    QString get_ipstr() const;
    // 获取连接的服务器IP地址
    unsigned short get_port() const;
    // 获取服务器连接状态
    int get_connectstatus() const;

    // 用于与外界主线程进行同步
    void tcp_wait(); // 阻塞直到唤醒
    void tcp_signal(); // 唤醒

signals:
    // 自定义信号用于给服务器发送数据的信号函数接口，内部链接使用
    void sig_tcp_send_data(DataId, QByteArray);

    // 通知tcp服务器连接状态
    void sig_tcp_connection_status(QString, unsigned short, ServerConnectStatus);

    // 通知登录结果(登录主界面用于判断是否跳转信令控制界面)
    void sig_notice_login_result(LoginStatus,QString);

    // 通知相应模块显示目录或文件的item条目信号
    // 自定义信号用于给指定控件发送服务器磁盘分区信息进行显示
    void sig_tip_notice_driverinfo(QString, QString);
    // 用于通知相应模块处理接收的目录信息信号
    void sig_tip_notice_dirinfo(std::shared_ptr<DirInfo>);
    // 用于通知相应模块处理接收的文件信息信号
    void sig_tip_notice_fileinfo(std::shared_ptr<FileInfo>);

    // 通知相应模块下载文件信号
    void sig_notice_downloadfilehead_success(QString,QString,unsigned long long); // 文件头正确信号
    void sig_notice_downloadfilehead_error(QString,QString,QString,bool); // 文件头错误信号
    void sig_notice_downloadfilechunk(std::shared_ptr<DownLoadFileChunk>); // 文件块数据信号
    // 通知相应模块文件上传进度/结果信号
    void sig_notice_uploadfile_resp(std::shared_ptr<UploadFileResp>); // 服务器文件上传响应信号
    // 通知相应模块执行文件结果信号
    void sig_notice_executefile_result(bool,QString);
    // 通知相应模块执行文件结果信号
    void sig_notice_deletefile_result(bool,QString);

    // 远程监控功能
    // 通知相应模块加入服务器推送截图组成功或失败信号(方案B：传递RemoteWatchStatus状态码与错误信息)
    void sig_notice_joinremotewatch_result(RemoteWatchStatus status, QString msg);
    // 通知相应模块的服务器屏幕截图信号(方案A兼容留存)
    void sig_notice_remotewatch_screenpix(QPixmap);




    //用于发送接收的服务器转发消息以信号方式通知相应模块处理
    void sig_tip_notice_message(std::shared_ptr<Message>);
    //用于发送接收的服务器转发文件以信号方式通知
    void sig_tip_notice_file(std::shared_ptr<File>);


private:
    // 网络子线程，专门用于网络数据的发送和接收
    QThread* networkThread;
    QSemaphore sem; // 用于和外界主线程同步
    // 通信层
    // tcp长连接成员
    QTcpSocket* _socket;    // tcp长连接的socket对象
    QString _server_ip;   // tcp长连接服务器的IP地址
    unsigned short _server_port;    //tcp长连接服务器的端口号

    // tcp连接接收数据的缓冲区
    QByteArray _recv_buffer;    // 接收对端服务器数据的临时缓冲区
    bool _findflag_success;        // 查找本次接收数据包头标记FE FF，是否查找成功标记
    bool _recvhead_finish;      // 标识本次接收服务器一次完整的数据头部（数据id+数据length）是否接收完毕，用于黏包处理
    unsigned int _message_len;//保存本次接收chatserver的数据长度，4字节
    QByteArray _message_context;//保存本次接收chatserver的数据内容本身


    // 逻辑层：处理通信层接收并解析的数据，客户端的tcp通信层和逻辑层不进行分离
    std::map<DataId,std::function<void(QByteArray)>> _map_reqid_callback;
    void init_map_reqid_callback(); // 初始化map保存的数据ID和回调处理函数

    // 无参构造函数
    static std::shared_ptr<TcpMgr> _instance; // 静态单例对象
    TcpMgr(QObject* parent = nullptr, QString server_ip = SERVER_IPADDR, unsigned short server_port = SERVER_LISTEN_PORT);

    // 初始化连接服务器的socket对象状态
    void init_socket();

    // 接收tcp长连接的服务器发送的数据，通过QTcpSocket类的readyRead信号来采取信号槽机制实现异步自动读取，内部实现黏包处理
    void slot_tcpserver_recvdata();
    // 处理接收后的服务器数据
    void deal_tcpserver_recvdata(QByteArray data, int data_length);

    // 给tcp长连接的服务器发送数据，通过信号槽机制实现发送tcp数据的有序性，内部自动封装tcp数据包格式给服务器发送数据
    void slot_tcp_sendserver_data(DataId reqid, QByteArray data);

};


#endif // TCPMGR_H
