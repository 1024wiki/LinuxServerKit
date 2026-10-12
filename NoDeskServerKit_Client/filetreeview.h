#ifndef FILETREEVIEW_H
#define FILETREEVIEW_H

#include <QTreeView> // 添加文件目录展示
#include <QStandardItemModel> // 模型
#include <QStandardItem>
#include <QMenu>
#include <QProgressDialog>
#include <QThread>
#include "downloadfilework.h"
#include "uploadfilework.h"
#include "playerdialog.h"
#include <vector>
#include <unordered_map>
#include <QString>
#include "global.h"

class FileTreeView :public QTreeView
{
    Q_OBJECT
private:
    // 创建文件系统模型
    QStandardItemModel* model;
    // 创建文件操作菜单
    QMenu* fileMenu;
    // 保存当前选择的需要操作(下载、执行、删除、播放)的文件路径
    QString file_fullpath;
    // 工作子线程用于下载文件
    QThread* workthread;
    // 下载文件工作对象，每个对象表示一个下载的文件，这个会在工作线程中执行
    std::unordered_map<QString, DownLoadFileWork*> downloadwork_map;
    // 下载文件的进度条对象，每个对象表示一个下载的文件进度条，这个UI控件只能在主线程创建，通过跨线程信号槽机制连接对应work下载对象更新
    std::unordered_map<QString, QProgressDialog*> downloadprogress_map;
    // 上传文件工作对象，每个对象表示一个上传的文件，这个会在工作线程中执行
    std::unordered_map<QString, UploadFileWork*> uploadwork_map;
    // 上传文件的进度条对象，每个对象表示一个上传的文件进度条，这个UI控件只能在主线程创建，通过跨线程信号槽机制连接对应work上传对象更新
    std::unordered_map<QString, QProgressDialog*> uploadprogress_map;
    // 当前打开的流媒体播放窗口（同一时间仅允许打开一个播放窗口）
    PlayerDialog* playerdialog;
    // 流媒体RTSP服务端口（由连接界面在连接成功后同步设置，用于构造rtsp://拉流URL）
    unsigned short rtsp_port;

public:
    FileTreeView(QWidget *parent = nullptr);
    ~FileTreeView();

    // 在QTreeView下的指定父目录下增加一行子文件item，默认是直接插入到不可见根目录下
    void insert_filerow(std::vector<QString> child_infos, QIcon driveIcon, QStandardItem* parent = nullptr);
    // 删除指定父目录下的所有子文件item行
    void delete_filerow(int start = 0, int num = -1, QStandardItem* parent = nullptr);

    // 接收tcp插入文件项信号，在UI控件下插入文件项
    void slot_addFileInfo_item(std::shared_ptr<FileInfo>);

    // 链接删除所有文件项信号，删除所有文件项
    void slot_deletefileinfo_allitem();

    // 设置当前连接的流媒体RTSP服务端口号（由连接界面在连接成功后调用）
    void set_rtsp_port(unsigned short port);

    // 连接右键点击文件项信号，在判断单击到有效右键菜单时，显示文件操作菜单
    void slot_show_filemenu(const QPoint&);

    // 点击文件菜单项中的下载文件action，发送下载文件请求信息
    void slot_download_file();
    // 接收下载文件子线程的初始化设置进度条信号
    void slot_download_work_process_init(QString savepath, QString filepath, unsigned long long totalsize);
    // 更新下载文件子线程传出的下载进度条更新信号
    void slot_download_processupdate(QString savepath, QString filepath, unsigned long long totalsize, unsigned long long downloadsize);
    // 下载文件结果提示
    void slot_download_result(QString savepath, QString filepath, QString error, bool sucess);

    // 点击文件菜单项中的上传文件action，选择本地文件并切块上传到该文件所在远程目录
    void slot_upload_file();
    // 接收上传工作子线程的初始化创建进度条信号
    void slot_upload_work_process_init(QString localpath, QString remotepath, unsigned long long totalsize);
    // 更新上传文件子线程传出的上传进度条更新信号
    void slot_upload_processupdate(QString localpath, QString remotepath, unsigned long long totalsize, unsigned long long uploadsize);
    // 接收信令服务器上传响应(进度/结果)
    void slot_upload_resp(std::shared_ptr<UploadFileResp>);
    // 上传文件结果提示
    void slot_upload_result(QString localpath, QString remotepath, QString error, bool success);

    // 点击文件菜单项中的执行文件action，发送执行文件请求信息
    void slot_execute_file();
    // 接收服务器执行文件结果的信号
    void slot_execute_file_result(bool success, QString filePath);

    // 点击文件菜单项中的下载文件action，发送删除文件请求信息
    void slot_delete_file();
    // 接收服务器删除文件结果的信号
    void slot_delete_file_result(bool success, QString filePath);

    // 点击文件菜单项中的流媒体播放action，校验后缀并打开播放窗口拉流播放
    void slot_play_file();

signals:

};

#endif // FILETREEVIEW_H
