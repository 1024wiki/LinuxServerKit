#ifndef GLOBAL_H
#define GLOBAL_H

// QT类
#include <QMainWindow>
#include <QString>
#include <QFile>
#include <QMessageBox>
#include <QFileDialog>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <QListWidgetItem>
#include <QDateTime>
#include <QStandardPaths>
#include <QProgressDialog>
#include <QInputDialog>

// C++标准库
#include <thread>
#include <mutex>
#include <memory>
#include <iostream>

#define RECV_BUF_SIZE 4096 // 定义每次接收数据的缓冲区大小
// 0xFEFF->网络字节序：FE FF
#define HEAD_FLAG_FIRST 0xFE // 网络字节序的定义消息头部标记的第一个字节
#define HEAD_FLAG_SECOND 0xFF // 网络字节序的定义消息头部标记的第二个字节
#define DATA_LEN 4 // 定义数据长度字段所占字节数
#define DATA_ID  2 // 定义数据标识字段所占字节数
#define CHECK_SUM 2 // 定义和校验字段所占字节数

#define PER_SEND_FILE_SIZE 2048 // 定义每次发送文件数据块的字符长度

#define SERVER_IPADDR "192.168.137.3" // 服务器IP地址
#define SERVER_LISTEN_PORT 10086  // 服务器信令监听端口
#define SERVER_RTSP_PORT 8554     // 服务器RTSP流媒体监听端口

// libVLC 初始化选项：降低网络拉流缓冲延迟(局域网点播优化)
#define VLC_NETWORK_CACHING "--network-caching=200"

// 可流媒体播放的视频文件后缀白名单（用于右键菜单过滤）
#define PLAYABLE_VIDEO_SUFFIXES QStringList() << "mp4" << "mkv" << "avi" << "flv" << "mov" << "wmv" << "ts" << "m4v"

// 服务器连接状态提示
enum class ServerConnectStatus : unsigned short{
    STATUS_CONNECTED = 10001, // 连接状态
    STATUS_DISCONNECTED = 10002, // 断开连接状态
    STATUS_AGAIN_CONNECTED = 10003, // 已连接但是出现重复连接
    STATUS_CONNECT_ERROR = 10004, // 连接错误状态
    STATUS_TIP_DISCONNECTED = 10005 // 提示未连接状态
};

// 消息ID类型
enum class DataId : unsigned short
{
    NO_MESSAGE = 0, // 无效消息
    LOGIN_USER = 1000, // 远程登录请求(携带用户名和密码)，服务端校验系统账号后返回登录结果
    GET_DRIVERINFO = 1001, // 获取磁盘分区信息
    GET_DIRINFO = 1002, // 获取指定目录信息
    DOWNLOAD_FILE = 1003, // 下载指定路径下的文件
    EXECUTE_FILE = 1004,  // 执行指定路径下的文件
    DELETE_FILE = 1005,   // 删除指定路径下的文件
    JOIN_TCP_REMOTEWATCH = 1006, // 加入远程监控命令
    GET_REMOTEWATCH_SCREEN = 1007, // 获取远程监控的屏幕
    EXIT_TCP_REMOTEWATCH = 1008, // 退出远程监控命令
    MOUSE_REMOTE_CONTROL = 1009, // 鼠标控制远程命令
    KEYBOARD_REMOTE_CONTROL = 1015, // 键盘控制远程命令(按键按下/释放/输入字符)
    LOCK_REMOTE_CONTROL = 1010,  // 远程主机锁机命令
    UNLOCK_REMOTE_CONTROL = 1011, // 远程主机解锁命令
    UPLOAD_FILE = 1012,  // 上传指定目录下的文件(客户端分块发送，服务端组包)
    CANCEL_UPLOAD_FILE = 1013, // 取消上传指定文件(服务端丢弃后续该文件块并清理)

    SEND_MSG = 1100, // 发送消息
    SEND_FILE = 1101 // 发送文件
};

// 播放器状态枚举（用于libVLC事件回调转发到UI层）
enum class PlayerStatus : int {
    STATUS_IDLE = 0,       // 空闲未加载
    STATUS_OPENING = 1,    // 正在打开媒体
    STATUS_BUFFERING = 2,  // 缓冲中
    STATUS_PLAYING = 3,    // 播放中
    STATUS_PAUSED = 4,     // 暂停
    STATUS_STOPPED = 5,    // 停止
    STATUS_ENDED = 6,      // 播放结束
    STATUS_ERROR = 7       // 播放错误
};

// 登录结果枚举（信令服务器校验用户名密码后返回的登录状态）
enum class LoginStatus : int {
    LOGIN_SUCCESS = 0,        // 登录成功
    LOGIN_USERNAME_ERROR = 1, // 用户名不存在
    LOGIN_PASSWORD_ERROR = 2, // 密码错误
    LOGIN_SYSTEM_ERROR = 3    // 系统内部错误
};

// 远程桌面监控状态枚举(方案B：RTSP桌面推流)
enum class RemoteWatchStatus : int {
    WATCH_SUCCESS = 0,         // 加入监控成功(服务端有桌面，已启动桌面RTSP推流)
    WATCH_NO_DESKTOP = 1,      // 服务端无桌面环境(如Headless服务器未安装X11/Wayland或DISPLAY为空)
    WATCH_ENCODER_ERROR = 2,   // 服务端H.264编码器启动失败
    WATCH_SYSTEM_ERROR = 3     // 系统内部错误
};

// 文件消息结构
struct FileInfo{
    QString parent_path; // 父目录路径
    QString file_name; // 文件名
    QString last_time; // 最后访问时间
    QString permissions; // 允许的权限操作
    unsigned long long size; // 大小
    QString type; // 文件file或其他特殊文件special
    QString symlink_target; // 软链接文件的目标文件
    FileInfo(QString parent_path,QString file_name,QString last_time,QString permissions,unsigned long long size,QString type,QString symlink_target)
        :parent_path(parent_path),file_name(file_name),last_time(last_time),permissions(permissions),size(size),type(type),symlink_target(symlink_target){}
};

// 目录消息结构
struct DirInfo{
    QString parent_path; // 父目录路径
    QString dir_name; // 文件名
    QString last_time; // 最后访问时间
    QString permissions; // 允许的权限操作
    unsigned long long size; // 大小
    QString type; // 普通目录
    QString symlink_target; // 软连接目录的目标目录
    DirInfo(QString parent_path,QString dir_name,QString last_time,QString permissions,unsigned long long size,QString type,QString symlink_target)
        :parent_path(parent_path),dir_name(dir_name),last_time(last_time),permissions(permissions),size(size),type(type),symlink_target(symlink_target){}
};

// 下载文件头或文件块数据结构
struct DownLoadFileChunk{
    QString savePath; // 本地保存目录
    QString filePath; // 远程服务器文件所在路径
    bool head; // 是否为文件头
    unsigned long long length; // 文件块长度
    QByteArray data; // 文件二进制数据块
    bool next; // 是否还有文件块数据
    unsigned long long totallen; // 文件总长度
    int index; // 文件块序号
    DownLoadFileChunk(QString savePath,QString filePath,bool head,unsigned long long length,QByteArray data,bool next,unsigned long long totallen,int index)
        :savePath(savePath),filePath(filePath),head(head),length(length),data(data),next(next),totallen(totallen),index(index) {}
};

// 服务器回复的文件上传响应结构(服务端按已接收字节返回进度/结果)
struct UploadFileResp{
    int error;              // 0:成功(或进行中) 1:文件打开/写入失败 2:上传被取消 3:接收不完整
    unsigned long long recvlen; // 服务端已接收的该文件总字节数
    unsigned long long totallen; // 待上传文件总长度
    bool finishe;           // 是否为最后一块的结束响应
    QString remotepath;     // 远程保存文件绝对路径
    UploadFileResp(int error,unsigned long long recvlen,unsigned long long totallen,bool finishe,QString remotepath)
        :error(error),recvlen(recvlen),totallen(totallen),finishe(finishe),remotepath(remotepath) {}
};

// 文件结构体类型
struct File
{
    QString user;
    QString name;
    qint64 size;
    QString type;
    QByteArray content;
    QString time;
    File(QString user, QString fileName, qint64 Size, QString Type, QByteArray content, QString time)
        :user(user),name(fileName),size(Size),type(Type),content(content),time(time) {}
};

// 消息结构体类型
struct Message
{
    QString name;
    QString message;
    QString time;
    Message(QString name, QString messgae, QString time):name(name),message(messgae),time(time){}
};

#endif // GLOBAL_H
