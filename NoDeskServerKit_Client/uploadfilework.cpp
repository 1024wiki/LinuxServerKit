#include "uploadfilework.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QCoreApplication>
#include <QDebug>

// 每块最大字节数512KB(与设计文档一致)
#define UPLOAD_CHUNK_SIZE (512*1024)

UploadFileWork::UploadFileWork(QString localpath, QString remotepath, QObject *parent)
    : QObject(parent),uploading(true),localpath(localpath),remotepath(remotepath),totallen(0),sentlen(0)
{
}

UploadFileWork::~UploadFileWork()
{
}

// 取消上传标记槽函数
void UploadFileWork::slot_uploadfile_cancelflag(){
    uploading = false;
    qDebug()<<"用户取消上传："<<localpath;
}

// 发起上传主流程(在工作子线程中执行)
void UploadFileWork::start_upload(){
    // 1. 打开本地文件并获取总长度
    QFile file(localpath);
    if(!file.open(QIODevice::ReadOnly)){
        emit sig_notice_upload_result(localpath,remotepath,"本地文件无法打开，请检查是否有权限或已经被使用。",false);
        return;
    }
    totallen = static_cast<unsigned long long>(file.size());
    sentlen = 0;
    uploading = true;

    // 2. 循环切块发送(512KB一块)，每块构造UPLOAD_FILE的JSON请求发送给服务端
    int index = 0;
    while(!file.atEnd()){
        // 取消上传：中断循环并向服务端发送取消上传请求
        if(!uploading){
            QJsonObject jsoncancel;
            jsoncancel["localpath"] = localpath;
            jsoncancel["remotepath"] = remotepath;
            QJsonDocument jsondoccancel(jsoncancel);
            emit sig_tcp_send_data(DataId::CANCEL_UPLOAD_FILE,jsondoccancel.toJson());
            file.close();
            emit sig_notice_upload_result(localpath,remotepath,"用户取消上传",false);
            return;
        }

        QByteArray chunk = file.read(UPLOAD_CHUNK_SIZE);
        if(chunk.isEmpty())
            break;

        // 构造当前块的上传请求
        QJsonObject jsonobj;
        jsonobj["localpath"] = localpath;   // 本地上传文件路径(回传供客户端匹配任务)
        jsonobj["remotepath"] = remotepath; // 远程保存文件绝对路径(目录下同名文件)
        jsonobj["offset"] = static_cast<qint64>(sentlen);      // 文件偏移位置
        jsonobj["totallen"] = static_cast<qint64>(totallen);   // 文件总长度
        jsonobj["next"] = !file.atEnd();     // 是否还有下一块(读取到末尾则为false)
        jsonobj["index"] = index;            // 文件块序号(从0开始)
        jsonobj["length"] = static_cast<qint64>(chunk.size());  // 本块原始长度
        jsonobj["data"] = QString::fromLatin1(chunk.toBase64()); // 文件块数据Base64编码
        QJsonDocument jsondoc(jsonobj);
        // 通过信号槽排队发送，保证与其它信令数据的发送有序性
        emit sig_tcp_send_data(DataId::UPLOAD_FILE,jsondoc.toJson());

        sentlen += static_cast<unsigned long long>(chunk.size());
        index++;
        // 通知主线程更新上传进度条
        emit sig_notice_upload_processupdate(localpath,remotepath,totallen,sentlen);
        // 按块节奏让出子线程事件循环，避免瞬间冲垮网络通道(流控)
        QThread::msleep(5);
    }
    file.close();

    // 3. 发送完成(最后一块next已为false)，等待服务端校验结果响应
    qDebug()<<QString("文件:%1全部%2字节已发送完毕，等待服务端校验。").arg(localpath).arg(totallen);
}
