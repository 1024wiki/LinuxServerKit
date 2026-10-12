#include "dirtreeview.h"
#include "global.h"
#include "tcpmgr.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <QFileInfo>
#include <QDir>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QIcon>

DirTreeView::DirTreeView(QWidget *parent):QTreeView(parent){
    // 初始化，文件系统负责保存数据，QTreeView负责展示数据
    model = new QStandardItemModel(this); // 将文件系统设置在QTreeView中
    this->setModel(model);
    // 设置列展示item
    model->setHorizontalHeaderLabels(QStringList() << "目录名称" << "软链接时目标" << "子目录数量" << "权限" << "最后访问时间");
    this->setColumnWidth(0, 200);  // 名称
    this->setColumnWidth(1, 80);  // 软链接目标
    this->setColumnWidth(2, 70);  // 子文件目录数量
    this->setColumnWidth(3, 70);  // 权限
    this->setColumnWidth(4, 110);  // 修改时间
    this->setAlternatingRowColors(true); // 按照奇偶行交替背景色
    this->setSortingEnabled(true); // 允许点击列标题进行排序

    // 连接信号槽，点击目录时，发送获取目录信息请求
    connect(this->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &DirTreeView::slot_getDirInfo);
    // 获取服务器返回的目录信息进行显示
    connect(TcpMgr::get_instance().get(),&TcpMgr::sig_tip_notice_dirinfo, this, &DirTreeView::slot_insertDirInfo);
}


// 在QTreeView下的指定父目录下增加子目录，默认是直接插入到不可见根目录下
void DirTreeView::insert_dirrow(std::vector<QString> child_infos, QIcon Icon, QStandardItem* parent){
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

// 删除指定父目录下的所有子目录
void DirTreeView::delete_dirrow(int start, int num, QStandardItem* parent){
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


// 获取当前选中的目录索引对应QStandardItem的绝对路径
QString DirTreeView::getDirPath(const QModelIndex &index) {
    // index是顶层不可见目录无效
    if (!index.isValid())
        return "";

    QStringList pathParts;
    QModelIndex current = index;
    // 从当前节点向上遍历到根节点
    while (current.isValid()) {
        QStandardItem* item = model->itemFromIndex(current); // 获取当前索引的item
        if (item)
            pathParts.prepend(item->text());
        current = current.parent();
    }

    // 构建完整绝对路径
    QString paths = "";
    for(QString& pathPart:pathParts)
        if(pathPart != "/")
            paths += pathPart + "/";
        else
            paths += pathPart; // linux文件下是以"/"，而不是Windows下的盘符"C:"
    // 返回绝对路径
    return paths;
}

// 根据绝对路径查找对应的QStandardItem
QStandardItem* DirTreeView::getItemByDirPath(const QString& absolutePath) {
    if (absolutePath.isEmpty())
        return nullptr;

    QStringList pathParts; // 储存绝对路径分割部分
    //  解析绝对路径，获取路径组件
    if(absolutePath.at(0)=='/')
        pathParts.append("/"); // linu文件下是以"/"开始
    pathParts += absolutePath.split("/",Qt::SkipEmptyParts); // Windows下"//"和Linux下"/"均可以正常处理
    if (pathParts.isEmpty())
        return nullptr;

    // 从根节点开始逐级查找
    QStandardItem* currentItem = model->invisibleRootItem();

    for (const QString& part : pathParts) {
        bool found = false;
        // 在当前节点的子节点中查找匹配的项
        for (int row = 0; row < currentItem->rowCount(); ++row) {
            QStandardItem* childItem = currentItem->child(row, 0);  // 第0列是名称
            if (childItem && childItem->text() == part) {
                currentItem = childItem;
                found = true;
                break;
            }
        }
        if (!found)
            return nullptr;  // 路径不存在
    }

    return currentItem;
}

// 获取当前选中的目录信息
void DirTreeView::slot_getDirInfo(const QModelIndex &current, const QModelIndex &previous){
    Q_UNUSED(previous);
    // 判断当前选择项的有效性
    if (!current.isValid()) {
        qDebug()<<"无效的目录选择。";
        return;
    }

    // 获取当前项的QStandardItem对象
    QStandardItem* currentItem = model->itemFromIndex(current);
    if (!currentItem) return;

    // 获取当前项的绝对路径
    QString path = getDirPath(current);
    qDebug()<<"当前所选项的绝对路径："<<path;
    if(path.isEmpty()) return;

    // 删除当前目录下的所有已存在的子目录信息，避免重复加载
    delete_dirrow(0,-1,currentItem);
    // 通知删除当前目录下的所有已存在的子文件信息
    emit sig_notice_remvfile();

    // 构造json数据发送请求获取当前路径目录信息，然后通过信号方式进行发送
    QJsonObject jsonobj; // 保存发送数据作为json数据
    jsonobj["path"] = path;
    QJsonDocument jsondoc(jsonobj);
    QByteArray json=jsondoc.toJson();
    emit TcpMgr::get_instance()->sig_tcp_send_data(DataId::GET_DIRINFO, json);
    qDebug()<<"发送的请求服务器获取的目录信息："<<json;
}

// 将服务器返回的目录信息进行展示
void DirTreeView::slot_insertDirInfo(std::shared_ptr<DirInfo> dirinfo){
    // 获取目录绝对路径上的最后父结点的item
    QStandardItem* parent_item = getItemByDirPath(dirinfo->parent_path);
    if(parent_item==nullptr)
        return;
    // 插入子目录
    QFileIconProvider iconProvider;
    QIcon Icon = iconProvider.icon(QFileIconProvider::Folder);
    std::vector<QString> vect={dirinfo->dir_name,dirinfo->symlink_target,QString::number(dirinfo->size),dirinfo->permissions,dirinfo->last_time};
    insert_dirrow(vect,Icon,parent_item); // 插入一条子目录
    qDebug()<<"插入目录"<<dirinfo->dir_name<<"的item成功";
}


