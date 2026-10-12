#ifndef DIRTREEVIEW_H
#define DIRTREEVIEW_H

#include <QTreeView> // 添加文件目录展示
#include <QStandardItemModel> // 模型
#include <QStandardItem>
#include <QModelIndex>
#include <vector>
#include <QString>
#include "global.h"

class DirTreeView: public QTreeView
{
    Q_OBJECT
private:
    QStandardItemModel *model; // 文件系统模型

public:
    DirTreeView(QWidget *parent = nullptr);
    ~DirTreeView()=default;

    // 在QTreeView下的指定父目录下增加子目录，默认是直接插入到不可见根目录下
    void insert_dirrow(std::vector<QString> child_infos, QIcon driveIcon, QStandardItem* parent = nullptr);
    // 删除指定父目录下的所有子目录
    void delete_dirrow(int start = 0, int num = -1, QStandardItem* parent = nullptr);

    // 获取当前选中的目录的绝对路径
    QString getDirPath(const QModelIndex &index);
    // 根据绝对路径查找对应的QStandardItem
    QStandardItem* getItemByDirPath(const QString& absolutePath);

    // 获取当前选中的目录绝对路径给服务器发送请求该目录信息
    void slot_getDirInfo(const QModelIndex &current, const QModelIndex &previous);
    // 将服务器返回的目录信息进行展示
    void slot_insertDirInfo(std::shared_ptr<DirInfo> dirinfo);

signals:
    // 通知对应文件目录控件删除所有文件信息
    void sig_notice_remvfile();
};

#endif // DIRTREEVIEW_H
