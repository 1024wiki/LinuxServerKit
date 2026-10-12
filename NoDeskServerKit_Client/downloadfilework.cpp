#include "downloadfilework.h"
#include "tcpmgr.h"

DownLoadFileWork::DownLoadFileWork(QString sourcesavepath, QString destfilepath, QObject *parent)
    : QObject(parent),download(true),success(true),sourcesavepath(sourcesavepath),destfilepath(destfilepath)
{
    QString filePath = QDir(sourcesavepath).filePath(QDir(destfilepath).dirName());
    file.reset(new QFile(filePath));
    file->open(QIODevice::WriteOnly | QIODevice::Append);
    // 连接tcp的下载文件信号，执行下载文件槽函数
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_downloadfilechunk,this,&DownLoadFileWork::slot_downloadfile);
}

// 清理资源
void DownLoadFileWork::clear(){
    if(file){
        // RAII原则，没有下载成功就删除文件
        if(file->isOpen())
            file->close();
        if(!success || !download){
            QString filePath = file->fileName();
            QFile::remove(filePath);
        }
        file.reset();
    }
}

DownLoadFileWork::~DownLoadFileWork(){
    clear();
}

// 标记取消下载文件
void DownLoadFileWork::slot_downloadfile_cancelflag(){
    download = false; // 取消下载文件
}

// 下载文件槽函数：将文件数据写入写缓冲区，当缓冲区满时然后一次性写入文件中，然后情况缓冲区
void DownLoadFileWork::slot_downloadfile(std::shared_ptr<DownLoadFileChunk> fileChunk){
    // 判断是否是本下载的文件块数据
    if(!file || file->fileName()!=QDir(fileChunk->savePath).filePath(QDir(fileChunk->filePath).dirName()))
        return;
    // 判断是否判断是否是合法的文件块数据
    if (!success){
        qDebug()<<"无效的文件块！";
        return;
    }
    if (!download){
        qDebug()<<"用户取消下载";
        // 发送下载失败的错误信号
        success = false;
        emit sig_notice_download_result(fileChunk->savePath,fileChunk->filePath,"用户取消下载",false);
        clear(); // 清理资源
        return;
    }
    // 追加写入文件
    if (file && (file->isOpen() || file->open(QIODevice::WriteOnly | QIODevice::Append))) {
        unsigned long long writelen = file->write(fileChunk->data); // 写入接收的文件块数据
        if(writelen != fileChunk->length){
            // 文件块失败，直接终止下载
            // 发送下载失败信号
            success = false; // 不需要继续下载
            emit sig_notice_download_result(fileChunk->savePath,fileChunk->filePath,"写入本地文件数据失败",false);
            clear(); // 清理资源
            qDebug() << "下载文件失败！";
            return;
        }
        // 判断写入后的文件长度是否达到总长度
        file->flush();  // 重要：刷新缓冲区，确保数据写入文件
        unsigned long long totalsize = file->size(); // 获取写入文件长度
        // 发送当前下载的数据长度，更新主线程中进度条
        emit sig_notice_download_processupdate(fileChunk->savePath,fileChunk->filePath,fileChunk->totallen,totalsize);
        // 判断是否接收完所有文件块数据，接收完确定是否下载完整文件，发送下载结果
        if(!(fileChunk->next)){
            if(totalsize == fileChunk->totallen){
                // 发送下载成功信号
                emit sig_notice_download_result(fileChunk->savePath,fileChunk->filePath,"",true);
                qDebug() << "下载文件成功！";
            }
            else{
                // 如果文件块结束，但是长度不同，说明下载文件失败
                success = false; // 不需要继续下载
                // 发送下载失败的错误信号
                emit sig_notice_download_result(fileChunk->savePath,fileChunk->filePath,"下载的文件不完整",false);
                clear(); // 清理资源
                qDebug() << "下载文件失败！";
            }
        }
    }
    else{
        qDebug()<<"打开本地文件失败！！！";
        success = false; // 不需要继续下载
        // 发送下载失败的错误信号
        emit sig_notice_download_result(fileChunk->savePath,fileChunk->filePath,"本地文件无法打开，请检查是否有权限或已经被使用。",false);
        clear(); // 清理资源
        return;
    }
}
