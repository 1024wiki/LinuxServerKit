#ifndef _TCPCONNECTION_H_
#define _TCPCONNECTION_H_

#include "ReadBuf.hpp"
#include "WriteBuf.hpp"
#include "Channel.hpp"
#include "EpollTaskScheduler.h"

// Tcp业务处理类：对Channel的进一步封装，用于处理Channel事件的上层业务数据处理回调函数
class TcpConnection: public std::enable_shared_from_this<TcpConnection> {
public:
    using TcpServerDeleteCallback = std::function<void(int)>;
private:
    // 关闭状态
    std::atomic<bool> _close;
    // 连接sockfd的事件管理Channel
    std::shared_ptr<Channel> _channel;
    // 互斥锁
    std::mutex _dealdata_mutex; // 对接收的数据进行处理时需要加锁
    // TcpServer服务器清理TcpConnection资源逻辑
    TcpServerDeleteCallback _tcpserver_deletecb;

protected: // 子类可见可用
    // 所在反应器指针，用于方便添加本连接添加定时任务和更新Channel数据
    std::weak_ptr<EpollTaskScheduler> _actor;
    // 读缓冲区组件，用于从sockfd中读取网络数据
    ReadBuf _recvbuf;
    // 写缓冲区组件，用一个队列维护发送数据包顺序，并从sockfd中发送网络数据
    WriteBuf _sendbuf;

private:
    // TcpConnection层的Channel事件的回调函数处理逻辑
    Channel::SocketEventCallback _read_tcpcb; // 接收事件回调处理逻辑
    Channel::SocketEventCallback _write_tcpcb; // 发送事件回调处理逻辑
    Channel::SocketEventCallback _close_tcpcb; // 关闭连接事件回调处理逻辑
    Channel::SocketEventCallback _errno_tcpcb; // 错误事件回调处理逻辑
    // 相应的处理逻辑
    virtual void HandleReadEventCallback();     // 接收事件处理逻辑
    virtual void HandleWriteEventCallback();    // 发送事件处理逻辑
    virtual void HandleCloseEventCallback();    // 关闭连接事件处理逻辑
    virtual void HandleErrorEventCallback();    // 错误是事件处理逻辑
    
public:
    // 上层应用层的相应TcpConnection事件处理后的业务处理逻辑，上层继承并设置
    using BussinessReadEventCallback = std::function<void(ReadBuf&)>;
    using BussinessCloseEventCallback = std::function<void(std::shared_ptr<TcpConnection>)>;
    using BussinessErrorEventCallback = std::function<void(std::shared_ptr<TcpConnection>)>;
    // 相应的设置函数，由上层继续并进行设置
    inline void SetReadTcpCBDealCB(BussinessReadEventCallback rd_dealcb){ _read_tcpcb_dealcb = rd_dealcb; }
    inline void SetCloseTcpCBDealCB(BussinessCloseEventCallback close_dealcb){ _close_tcpcb_dealcb = close_dealcb; }
    inline void SetErrorTcpCBDealCB(BussinessErrorEventCallback errno_dealcb){ _errno_tcpcb_dealcb = errno_dealcb; }
protected:
    BussinessReadEventCallback _read_tcpcb_dealcb; // 上层业务层的读取数据的具体解析处理逻辑
    BussinessCloseEventCallback _close_tcpcb_dealcb; // 上层业务层的关闭连接的资源释放等逻辑
    BussinessErrorEventCallback _errno_tcpcb_dealcb; // 上层业务层的错误处理逻辑
    
public:
    TcpConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> taskscheduler);
    virtual ~TcpConnection();
    // 断开连接并释放资源(actor保存的Channel、上层业务关闭逻辑、tcpserver保存的Connection、Channel的套接字sockfd关闭)
    void CloseTcpConnection();  
    
    // Tcp服务器资源清理客户端逻辑
    inline void SetTcpServerDeleteCallback(TcpServerDeleteCallback tcpserver_deletecb) { _tcpserver_deletecb = tcpserver_deletecb; }

    // Sock数据发送
    void Send(Packet&& packet);
    void Send(uint32_t total_len, std::shared_ptr<char[]> data);
    void Send(uint32_t total_len, const char* data);

    // 获取sockfd套接字
    inline int GetSockFD() const{ return _channel->GetSockFD(); }
};

#endif