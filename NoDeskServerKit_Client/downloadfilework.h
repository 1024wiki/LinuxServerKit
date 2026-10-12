#ifndef DOWNLOADFILEWORK_H
#define DOWNLOADFILEWORK_H
#include "global.h"

class DownLoadFileWork: public QObject
{
    Q_OBJECT
private:
    bool download; // 是否取消下载
    bool success; // 下载文件块有效
    //QByteArray filebuf; // 文件接收缓冲区
    QString sourcesavepath; // 保存本地文件路径
    QString destfilepath; // 下载远程文件路径
    std::shared_ptr<QFile> file; // 当前接收的文件对象

    // 清理资源
    void clear();

public:
    DownLoadFileWork(QString sourcesavepath, QString destfilepath, QObject* parent = nullptr);
    ~DownLoadFileWork();

    // 下载文件槽函数
    void slot_downloadfile(std::shared_ptr<DownLoadFileChunk> fileChunk);
    // 标记取消下载文件槽函数
    void slot_downloadfile_cancelflag();

signals:
    // 通知更新下载文件进度条
    void sig_notice_download_processupdate(QString,QString,unsigned long long,unsigned long long);
    // 通知下载文件结果信号
    void sig_notice_download_result(QString,QString,QString,bool);
};

#endif // DOWNLOADFILEWORK_H
