#include "filetreeview.h"
#include <QAction>
#include <QPoint>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QApplication>
#include <QStyle>
#include <QIcon>
#include <QModelIndex>
#include "tcpmgr.h"

// 构造函数
FileTreeView::FileTreeView(QWidget *parent):QTreeView(parent) {
    // 初始化UI控件和成员变量
    // 初始化，文件系统负责保存数据，QTreeView负责展示数据
    model = new QStandardItemModel(this); // 将文件系统设置在QTreeView中
    this->setModel(model);
    // 设置列展示item
    model->setHorizontalHeaderLabels(QStringList() << "文件名称" << "软链接时目标" << "字节数大小" << "权限" << "最后访问时间" << "父目录绝对路径");
    this->setColumnWidth(0, 200);  // 名称
    this->setColumnWidth(1, 80);  // 软链接目标
    this->setColumnWidth(2, 70);  // 子文件目录数量
    this->setColumnWidth(3, 70);  // 权限
    this->setColumnWidth(4, 110);  // 修改时间
    this->setAlternatingRowColors(true); // 按照奇偶行交替背景色
    this->setSortingEnabled(true); // 允许点击列标题进行排序
    // 隐藏第5列存放的额外父目录绝对路径信息
    // this->hideColumn(5);

    // 初始化文件菜单
    fileMenu = new QMenu(this);
    QStyle* style = QApplication::style();
    // 文件下载操作项
    QIcon saveIcon = style->standardIcon(QStyle::SP_ArrowDown);
    QAction* download = new QAction(saveIcon,"下载文件",fileMenu);
    fileMenu->addAction(download);

    // 文件运行操作项
    QIcon executeIcon = style->standardIcon(QStyle::SP_MediaPlay);
    QAction* execute = new QAction(executeIcon,"运行文件",fileMenu);
    fileMenu->addAction(execute);

    // 文件删除操作项
    QIcon deleteIcon = style->standardIcon(QStyle::SP_TrashIcon);
    QAction* deletion = new QAction(deleteIcon,"删除文件",fileMenu);
    fileMenu->addAction(deletion);

    // 文件上传操作项（选择本地文件切块上传到当前文件所在远程目录）
    QIcon uploadIcon = style->standardIcon(QStyle::SP_ArrowUp);
    QAction* upload = new QAction(uploadIcon,"上传文件",fileMenu);
    fileMenu->addAction(upload);

    // 文件流媒体播放操作项（设计文档新增：通过libVLC拉流点播）
    QIcon playIcon = style->standardIcon(QStyle::SP_MediaPlay);
    QAction* play = new QAction(playIcon,"流媒体播放",fileMenu);
    fileMenu->addAction(play);
    // 设置上下文菜单策略
    this->setContextMenuPolicy(Qt::CustomContextMenu);


    // 连接信号槽
    // 连接tcp插入文件信息item信号
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_tip_notice_fileinfo,this,&FileTreeView::slot_addFileInfo_item);

    // 右键单击文件信息出现文件操作菜单项
    connect(this,&FileTreeView::customContextMenuRequested,this,&FileTreeView::slot_show_filemenu);
    // 连接下载文件信号槽
    connect(download,&QAction::triggered,this,&FileTreeView::slot_download_file);
    // 连接文件头正确或错误信号
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_downloadfilehead_error,this,&FileTreeView::slot_download_result); // 文件头错误信号
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_downloadfilehead_success,this,&FileTreeView::slot_download_work_process_init); // 文件头正确信号
    // 连接删除文件信号槽
    connect(deletion,&QAction::triggered,this,&FileTreeView::slot_delete_file);
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_deletefile_result,this,&FileTreeView::slot_delete_file_result);
    // 连接上传文件信号槽
    connect(upload,&QAction::triggered,this,&FileTreeView::slot_upload_file);
    // 连接信令服务器上传响应信号槽(进度/结果)
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_uploadfile_resp,this,&FileTreeView::slot_upload_resp);
    // 连接执行文件信号槽
    connect(execute,&QAction::triggered,this,&FileTreeView::slot_execute_file);
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_notice_executefile_result,this,&FileTreeView::slot_execute_file_result);
    // 连接流媒体播放信号槽
    connect(play,&QAction::triggered,this,&FileTreeView::slot_play_file);

    // 初始化当前播放窗口指针与RTSP端口默认值
    playerdialog = nullptr;
    rtsp_port = SERVER_RTSP_PORT;

    // 初始化工作子线程
    workthread = new QThread(this);
    workthread->start(); // 默认执行run函数开启工作子线程的事件循环
}

// 析构函数
FileTreeView::~FileTreeView(){
    // 关闭尚未关闭的播放窗口
    if(playerdialog){
        playerdialog->close();
        playerdialog->deleteLater();
        playerdialog = nullptr;
    }
    // 析构前，等待子线程的运行结束
    if(workthread->isRunning()){
        workthread->quit(); // 请求子线程退出事件循环
        workthread->wait(); // 等待子线程退出事件循环
        workthread->deleteLater(); // 自动释放
    }
    // 退出前释放没有指定父对象的工作对象资源
    for(auto& pair:downloadwork_map)
        (pair.second)->deleteLater();
    for(auto& pair:uploadwork_map)
        (pair.second)->deleteLater();
}

// 在QTreeView下的指定父目录下增加子文件，默认是直接插入到不可见根目录下
void FileTreeView::insert_filerow(std::vector<QString> child_infos, QIcon Icon, QStandardItem* parent){
    if(parent == nullptr)
        parent = model->invisibleRootItem(); // 获取不可见根结点
    // 构建子目录属性行
    QList<QStandardItem*> child; // 子目录属性行
    for(QString& info:child_infos){
        QStandardItem* item = new QStandardItem(info);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); // 设置为不可编辑
        child.append(item);
    }
    // 设置图标
    child.at(0)->setIcon(Icon);

    // 在父目录下添加子目录
    parent->appendRow(child);
}

// 删除指定父目录下的所有子文件
void FileTreeView::delete_filerow(int start, int num, QStandardItem* parent){
    if(parent == nullptr)
        parent = model->invisibleRootItem(); // 获取不可见根结点
    // 获取子项个数，删除指定子项
    if(num < 0){
        // 删除从start开始的所有子项
        int rowCount = parent->rowCount();
        parent->removeRows(start, rowCount);
    }
    else
        // 删除从start开始的指定个数子项
        parent->removeRows(start, num);
}

// 接收tcp插入文件信号在UI控件下显示
void FileTreeView::slot_addFileInfo_item(std::shared_ptr<FileInfo> fileinfo){
    // 插入子文件
    QFileIconProvider iconProvider;
    // 选择一个和文件后缀名相匹配的文件图标
    QIcon Icon = iconProvider.icon(QFileIconProvider::File);
    std::vector<QString> vect={fileinfo->file_name,fileinfo->symlink_target,QString::number(fileinfo->size),fileinfo->permissions,fileinfo->last_time,fileinfo->parent_path};
    insert_filerow(vect,Icon); // 默认插入在顶层不可见根目录下
    qDebug()<<"插入文件"<<fileinfo->file_name<<"的item成功";
}

// 链接删除所有文件信号
void FileTreeView::slot_deletefileinfo_allitem(){
    delete_filerow();
}

// 设置当前连接的流媒体RTSP服务端口号
void FileTreeView::set_rtsp_port(unsigned short port){
    rtsp_port = port;
    qDebug()<<"FileTreeView更新RTSP端口号："<<rtsp_port;
}

// 右键文件项，显示文件操作菜单
void FileTreeView::slot_show_filemenu(const QPoint& pos){
    // 获取点击位置的索引，包括(row,col,parent,isvalid)
    QModelIndex index = this->indexAt(pos);
    if (!index.isValid()) return;

    // 获取点击索引位置所在文件item的文件名和文件所在父目录路径的索引
    QModelIndex filenameColIndex = index.siblingAtColumn(0); // 文件名
    QModelIndex filepathColIndex = index.siblingAtColumn(5); // 文件目录所在路径
    if (!filenameColIndex.isValid() || !filepathColIndex.isValid()){
        qDebug() << "获取到无效的文件名或路径。";
        return;
    }
    // 通过模型获取索引对应的QStandardItem的条目的文本信息
    QStandardItem* fileItem = model->itemFromIndex(filenameColIndex);
    QStandardItem* pathItem = model->itemFromIndex(filepathColIndex);
    if (!fileItem || !pathItem){
        qDebug() << "获取到无效的文件名或路径。";
        return;
    }
    // 保存当前需要操作的文件路径，这里默认被控制服务端为Linux，路径连接符使用"/"
    file_fullpath  = pathItem->text() + fileItem->text();
    qDebug()<<"输出的文件路径："<<file_fullpath;
    // 在右键点击相对位置转化为全局位置，显示文件操作菜单
    fileMenu->exec(this->viewport()->mapToGlobal(pos));
}

// 点击文件菜单项中的下载文件action，发送下载文件请求信息
void FileTreeView::slot_download_file(){
    qDebug()<<"所需下载的文件完整路径："<<file_fullpath;

    // 获取本地保存该文件的目录路径
    QString savepath=QFileDialog::getExistingDirectory(this,"选择一个保存文件数据的本地目录",QDir::homePath());//保存打开指定目录下的对话窗口所选择的目录的绝对路径
    if(savepath.isEmpty()){
        QMessageBox::critical(this,"错误","选择的保存本地目录为空！");
        return;
    }
    int ret = QMessageBox::question(this,"提示",QString("下载的目标文件远程路径为：%1，保存的本地目录为：%2。").arg(file_fullpath).arg(savepath),
                          QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save);
    if(ret==QMessageBox::Cancel)
        return;

    // 判断是否已经有该路径下的文件在下载，存在时直接返回
    QString Path = QDir(savepath).filePath(QDir(file_fullpath).dirName()); // 本地保存路径
    if(downloadprogress_map.find(Path)!=downloadprogress_map.end()||downloadwork_map.find(Path)!=downloadwork_map.end()){
        QMessageBox::information(this, "下载文件提示", QString("远程文件:%1正在本地目录%2中下载！").arg(file_fullpath).arg(savepath));
        return;
    }

    // 包装成json数据发送给服务器，请求服务器传输文件信息
    // 构造json数据发送请求获取当前路径目录信息，然后通过信号方式进行发送
    QJsonObject jsonobj; // 保存发送数据作为json数据
    jsonobj["sourcesavepath"] = savepath; // 保存文件路径
    jsonobj["destfilepath"] = file_fullpath; // 下载的服务器文件路径
    QJsonDocument jsondoc(jsonobj);
    QByteArray json=jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::DOWNLOAD_FILE, json);
    qDebug()<<"发送的请求服务器获取的下载文件信息："<<json;
}

// 接收下载文件子线程的初始化设置进度条信号
void FileTreeView::slot_download_work_process_init(QString savepath, QString filepath, unsigned long long totalsize){
    QString Path = QDir(savepath).filePath(QDir(filepath).dirName()); // 本地保存路径
    // 创建文件下载进度条
    QProgressDialog* download_progress = new QProgressDialog(QString("正在下载文件:%1到%2中(0/%3)...").arg(filepath).arg(savepath).arg(totalsize),"取消加载",0,100,this);
    downloadprogress_map.insert(std::make_pair(Path,download_progress));
    download_progress->setWindowTitle("正在下载文件："+filepath);//设置进度条对话窗口的标题，有参构造初始化没有设置标题
    download_progress->setAutoReset(true);//设置进度条对话窗口满进度时不重置进度条，默认为true表示重置进度条
    download_progress->setAutoClose(true);//设置进度条对话窗口满进度时是否关闭进度条(默认为true表示关闭进度条)，但如果setAutoReset(false)不会自动关闭进度条
    download_progress->setMinimumDuration(1000); // 最少显示时间1s
    download_progress->setValue(0); // 初始化进度条值为0
    download_progress->show();
    // 创建下载文件工作对象
    DownLoadFileWork* download_work = new DownLoadFileWork(savepath,filepath);
    downloadwork_map.insert(std::make_pair(Path,download_work));
    download_work->moveToThread(workthread); // 工作内容移动到工作子线程中完成
    // 连接信号槽
    // 连接取消下载和本地取消工作子线程下载信号
    connect(download_progress,&QProgressDialog::canceled,download_work,&DownLoadFileWork::slot_downloadfile_cancelflag);
    // 连接工作子线程和主线程通信文件下载通信的信号槽
    // 工作子线程下载文件更新下载进度条
    connect(download_work,&DownLoadFileWork::sig_notice_download_processupdate,this,&FileTreeView::slot_download_processupdate);
    // 工作子线程下载文件结果信号
    connect(download_work,&DownLoadFileWork::sig_notice_download_result,this,&FileTreeView::slot_download_result);

    // 唤醒阻塞的tcp线程
    TcpMgr::get_instance()->tcp_signal();
}
// 更新下载文件子线程传出的下载进度条更新信号
void FileTreeView::slot_download_processupdate(QString savepath, QString filepath, unsigned long long totalsize, unsigned long long downloadsize){

    // 找到相应的下载文件进度条进行更新
    QString Path = QDir(savepath).filePath(QDir(filepath).dirName()); // 本地保存路径
    std::unordered_map<QString,QProgressDialog*>::iterator it = downloadprogress_map.find(Path);
    if(it==downloadprogress_map.end())
        return; // 没找到直接返回
    int percent = static_cast<int>((downloadsize * 100) / totalsize); // 计算已下载百分比
    it->second->setValue(percent);
    it->second->setLabelText(QString("正在下载文件:%1到%2中(%3/%4)...").arg(filepath).arg(savepath).arg(downloadsize).arg(totalsize));
}
// 下载文件结果提示
void FileTreeView::slot_download_result(QString savepath, QString filepath, QString error, bool success){

    // 提示显示下载文件结果消息
    if(success)
        QMessageBox::information(this,"下载文件结果",QString("成功下载文件:%1到%2中。").arg(filepath).arg(savepath));
    else
        QMessageBox::warning(this,"下载文件结果",QString("下载文件:%1到%2中失败！错误原因:%3。").arg(filepath).arg(savepath).arg(error));

    // 清理下载文件进度条和工作对象
    QString Path = QDir(savepath).filePath(QDir(filepath).dirName()); // 本地保存路径
    downloadprogress_map.erase(Path);
    downloadwork_map.erase(Path);
}

// 点击文件菜单项中的上传文件action，选择本地文件并切块上传到该文件所在远程目录
void FileTreeView::slot_upload_file(){
    // 上传目标为当前选中文件所在的远程父目录(同名文件覆盖)，因此必须先有右键选中的文件
    if(file_fullpath.isEmpty()){
        QMessageBox::warning(this,"上传文件","请先在文件列表中右键选择一个目标文件！");
        return;
    }
    // 目标远程保存路径 = 该文件的父目录 + 上传文件名(服务端按同名文件覆盖)
    QString parent_dir = file_fullpath.left(file_fullpath.lastIndexOf('/') + 1);
    if(parent_dir.isEmpty()){
        QMessageBox::warning(this,"上传文件","获取目标远程目录失败！");
        return;
    }

    // 1. 打开本地文件选择框，选择待上传文件
    QString localpath = QFileDialog::getOpenFileName(this,"选择一个待上传的本地文件",QDir::homePath());
    if(localpath.isEmpty()){
        QMessageBox::critical(this,"错误","选择的待上传本地文件为空！");
        return;
    }
    // 远程保存路径 = 远程父目录 + 本地文件名
    QString remotepath = parent_dir + QFileInfo(localpath).fileName();
    if(QFileInfo(localpath).fileName().isEmpty()){
        QMessageBox::warning(this,"上传文件","无效的本地文件名！");
        return;
    }

    // 2. 判断是否已经有该远程路径下的文件在上传，存在时直接返回
    if(uploadprogress_map.find(remotepath)!=uploadprogress_map.end()||uploadwork_map.find(remotepath)!=uploadwork_map.end()){
        QMessageBox::information(this,"上传文件提示",QString("文件:%1正在上传到远程目录:%2中！").arg(localpath).arg(parent_dir));
        return;
    }

    // 3. 二次确认上传(同名文件将被服务端覆盖)
    unsigned long long totalsize = static_cast<unsigned long long>(QFileInfo(localpath).size());
    int ret = QMessageBox::question(this,"上传文件提示",
        QString("待上传的本地文件为：%1\n保存的远程路径为：%2（同名文件将被覆盖）。\n是否继续上传？")
            .arg(localpath).arg(remotepath),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
    if(ret==QMessageBox::Cancel)
        return;

    // 4. 创建上传进度条与上传工作对象
    slot_upload_work_process_init(localpath,remotepath,totalsize);
}

// 接收上传工作子线程的初始化创建进度条信号
void FileTreeView::slot_upload_work_process_init(QString localpath, QString remotepath, unsigned long long totalsize){
    // 创建文件上传进度条(取消按钮用于中断上传)
    QProgressDialog* upload_progress = new QProgressDialog(QString("正在上传文件:%1到%2中(0/%3)...").arg(localpath).arg(remotepath).arg(totalsize),"取消上传",0,100,this);
    uploadprogress_map.insert(std::make_pair(remotepath,upload_progress));
    upload_progress->setWindowTitle("正在上传文件："+QFileInfo(localpath).fileName());
    upload_progress->setAutoReset(true);
    upload_progress->setAutoClose(true);
    upload_progress->setMinimumDuration(0);
    upload_progress->setValue(0);
    upload_progress->show();

    // 创建上传文件工作对象并移动到工作子线程执行
    UploadFileWork* upload_work = new UploadFileWork(localpath,remotepath);
    uploadwork_map.insert(std::make_pair(remotepath,upload_work));
    upload_work->moveToThread(workthread);
    // 传输通道：上传工作对象的发送请求信号连接到TcpMgr的发送槽(排队保证有序)
    connect(upload_work,&UploadFileWork::sig_tcp_send_data,TcpMgr::get_instance().get(),&TcpMgr::sig_tcp_send_data,Qt::QueuedConnection);
    // 取消上传信号：进度条取消按钮 → 工作对象取消标记
    connect(upload_progress,&QProgressDialog::canceled,upload_work,&UploadFileWork::slot_uploadfile_cancelflag);
    // 工作子线程 → 主线程 上传进度更新
    connect(upload_work,&UploadFileWork::sig_notice_upload_processupdate,this,&FileTreeView::slot_upload_processupdate);
    // 工作子线程 → 主线程 上传结果
    connect(upload_work,&UploadFileWork::sig_notice_upload_result,this,&FileTreeView::slot_upload_result);

    // 通过队列化信号调用工作对象的上传主流程(在工作子线程事件循环中执行)
    QMetaObject::invokeMethod(upload_work,"start_upload",Qt::QueuedConnection);
}

// 更新上传文件子线程传出的上传进度条更新信号
void FileTreeView::slot_upload_processupdate(QString localpath, QString remotepath, unsigned long long totalsize, unsigned long long uploadsize){
    // 找到相应的上传文件进度条进行更新
    std::unordered_map<QString,QProgressDialog*>::iterator it = uploadprogress_map.find(remotepath);
    if(it==uploadprogress_map.end())
        return; // 没找到直接返回
    int percent = (totalsize>0)?static_cast<int>((uploadsize * 100) / totalsize):0; // 计算已上传百分比
    it->second->setValue(percent);
    it->second->setLabelText(QString("正在上传文件:%1到%2中(%3/%4)...").arg(localpath).arg(remotepath).arg(uploadsize).arg(totalsize));
}

// 接收信令服务器上传响应(进度/结果)
void FileTreeView::slot_upload_resp(std::shared_ptr<UploadFileResp> resp){
    // 未匹配到进行中的上传任务则忽略(可能为历史响应)
    std::unordered_map<QString,QProgressDialog*>::iterator it = uploadprogress_map.find(resp->remotepath);
    if(it==uploadprogress_map.end())
        return;

    // 服务端报告错误/取消/不完整
    if(resp->error!=0){
        QString reason = (resp->error==1)?"服务端写入远程文件失败":
                         (resp->error==2)?"上传已被服务端取消":"服务端接收的文件不完整";
        slot_upload_result("",resp->remotepath,reason,false);
        return;
    }

    // 进行中或最后一块结束响应，更新进度条
    int percent = (resp->totallen>0)?static_cast<int>((resp->recvlen * 100) / resp->totallen):0;
    it->second->setValue(percent);
    it->second->setLabelText(QString("正在上传文件到%1中(%2/%3)...").arg(resp->remotepath).arg(resp->recvlen).arg(resp->totallen));

    // 最后一块响应，提示上传成功并清理任务
    if(resp->finishe){
        if(resp->totallen==resp->recvlen)
            slot_upload_result("",resp->remotepath,"",true);
        else
            slot_upload_result("",resp->remotepath,"服务端接收的文件长度不完整",false);
    }
}

// 上传文件结果提示
void FileTreeView::slot_upload_result(QString localpath, QString remotepath, QString error, bool success){
    // 提示显示上传文件结果消息
    if(success)
        QMessageBox::information(this,"上传文件结果",QString("成功上传文件到:%1。").arg(remotepath));
    else
        QMessageBox::warning(this,"上传文件结果",QString("上传文件到:%1失败！错误原因:%2。").arg(remotepath).arg(error));

    // 清理上传文件进度条和工作对象
    uploadprogress_map.erase(remotepath);
    std::unordered_map<QString,UploadFileWork*>::iterator it = uploadwork_map.find(remotepath);
    if(it!=uploadwork_map.end()){
        (it->second)->deleteLater();
        uploadwork_map.erase(it);
    }
}

// 点击文件菜单项中的执行文件action，发送执行文件请求信息
void FileTreeView::slot_execute_file(){
    qDebug()<<"所需执行的文件完整路径："<<file_fullpath;
    // 构造json数据发送请求
    QJsonObject json;
    json["executefilepath"] = file_fullpath;
    QJsonDocument jsondoc(json);
    QByteArray jsonstr = jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::EXECUTE_FILE, jsonstr);
    qDebug()<<"发送的请求服务器获取的执行文件信息："<<jsonstr;
}
// 接收服务器执行文件结果的信号
void FileTreeView::slot_execute_file_result(bool success, QString filePath){
    if(!success)
        QMessageBox::warning(this,"执行文件",QString("执行文件:%1失败!").arg(filePath));
}


// 点击文件菜单项中的下载文件action，发送删除文件请求信息
void FileTreeView::slot_delete_file(){
    qDebug()<<"所需删除的文件完整路径："<<file_fullpath;
    // 提示是否确定删除文件
    int ret = QMessageBox::question(this,"删除文件提示",QString("是否确定删除文件:%1?").arg(file_fullpath),QMessageBox::Ok | QMessageBox::Cancel,QMessageBox::Cancel);
    if(ret == QMessageBox::Cancel)
        return;

    // 构造json数据发送请求
    QJsonObject json;
    json["deletefilepath"] = file_fullpath;
    QJsonDocument jsondoc(json);
    QByteArray jsonstr = jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::DELETE_FILE, jsonstr);
    qDebug()<<"发送的请求服务器获取的删除文件信息："<<jsonstr;
}
// 接收服务器删除文件结果的信号
void FileTreeView::slot_delete_file_result(bool success, QString filePath){
    // 删除文件成功，则删除该文件item
    if(success){
        QString fileName = QDir(filePath).dirName(); // 获取该路径的文件名
        // 从根节点开始查找该文件名所在的item位置，查找到进行删除
        int row = 0; // 标记查找到文件项所在row行位置
        QStandardItem* currentItem = model->invisibleRootItem();
        for (row = 0; row < currentItem->rowCount(); ++row) {
            QStandardItem* childItem = currentItem->child(row, 0);  // 第0列是名称
            if (childItem && childItem->text() == fileName) {
                break;
            }
        }
        // 查找到时就删除该文件item项
        if (row<currentItem->rowCount())
            delete_filerow(row,1); // 删除该文件item项
    }
    else
        QMessageBox::warning(this,"删除文件",QString("删除文件:%1失败!").arg(filePath));
}

// 点击文件菜单项中的流媒体播放action，校验后缀并打开播放窗口拉流播放
void FileTreeView::slot_play_file(){
    qDebug()<<"所需播放的文件完整路径："<<file_fullpath;

    // 1. 校验文件后缀是否在可播放白名单内
    QString suffix = QFileInfo(file_fullpath).suffix().toLower();
    QStringList playable = PLAYABLE_VIDEO_SUFFIXES;
    if(!playable.contains(suffix)){
        QMessageBox::warning(this,"流媒体播放",
            QString("文件:%1不是可播放的视频格式！\n支持的格式：%2").arg(file_fullpath).arg(playable.join(", ")));
        return;
    }

    // 2. 获取信令服务器连接的IP地址，用于构造RTSP URL
    QString server_ip = TcpMgr::get_instance()->get_ipstr();
    if(server_ip.isEmpty()){
        QMessageBox::warning(this,"流媒体播放","未连接服务器，无法获取服务器地址！");
        return;
    }
    // RTSP服务端监听端口（由连接界面在连接成功后同步设置）
    unsigned short rtsp_port_used = rtsp_port;
    // 构造RTSP URL：rtsp://ip:port/stream/<file_path>，设计文档Q5规范格式
    QString rtsp_url = QString("rtsp://%1:%2/stream/%3")
                            .arg(server_ip)
                            .arg(rtsp_port_used)
                            .arg(file_fullpath);

    qDebug()<<"构造的RTSP播放URL："<<rtsp_url;

    // 3. 若已有播放窗口打开，先关闭旧窗口，同一时间仅允许一个播放会话
    if(playerdialog && playerdialog->isVisible()){
        playerdialog->close();
        playerdialog->deleteLater();
        playerdialog = nullptr;
    }

    // 4. 创建播放窗口并开始拉流播放
    playerdialog = new PlayerDialog(this);
    playerdialog->setAttribute(Qt::WA_DeleteOnClose, false); // 关闭时保留对象，析构时统一释放
    if(!playerdialog->start_play(rtsp_url)){
        playerdialog->close();
        playerdialog->deleteLater();
        playerdialog = nullptr;
        return;
    }
    playerdialog->show();
    playerdialog->raise();
    playerdialog->activateWindow();
}
