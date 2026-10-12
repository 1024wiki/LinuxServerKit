#ifndef UPLOADFILEWORK_H
#define UPLOADFILEWORK_H
#include "global.h"

// UploadFileWork：文件上传工作对象
// 在工作子线程中将本地文件按512KB切块，经信令通道逐块发送给服务端组装；
// 通过信号槽与主线程UI(上传进度条)通信，并支持取消上传。
class UploadFileWork: public QObject
{
    Q_OBJECT
private:
    bool uploading;      // 是否继续上传(收到取消请求置false)
    QString localpath;   // 本地待上传文件路径
    QString remotepath;  // 远程保存文件绝对路径(目录下同名文件)
    unsigned long long totallen; // 文件总长度
    unsigned long long sentlen;  // 已发送字节数

public:
    UploadFileWork(QString localpath, QString remotepath, QObject* parent = nullptr);
    ~UploadFileWork();

    // 发起上传主流程(在工作子线程中执行)
    void start_upload();
    // 取消上传标记槽函数
    void slot_uploadfile_cancelflag();

signals:
    // 请求发送TCP数据(携带消息ID与JSON体)，由TcpMgr执行真正的网络发送
    void sig_tcp_send_data(DataId, QByteArray);
    // 通知更新上传进度条(本地路径,远程路径,总长度,已发送长度)
    void sig_notice_upload_processupdate(QString,QString,unsigned long long,unsigned long long);
    // 通知上传结果(本地路径,远程路径,错误原因,是否成功)
    void sig_notice_upload_result(QString,QString,QString,bool);
};

#endif // UPLOADFILEWORK_H
