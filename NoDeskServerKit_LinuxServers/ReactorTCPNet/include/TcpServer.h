#ifndef _TCPSERVER_H_
#define _TCPSERVER_H_

#include "EventLoop.h"
#include "TcpConnection.h"
#include <unordered_map>
#include <mutex>
class Acceptor; // 前向声明，避免循环依赖

// Tcp服务器
class TcpServer
{
public:
    // 反应器模式
    enum class ReactorModel:uint8_t
    {
        SINGLE = 1, // 单反应器模式
        MASTER_SLAVE, // 多反应器主从模式
        PEER_TO_PEER // 多反应器对等模式
    };

private:
    // Tcp服务器运行标志
    std::atomic<bool> _running;
    // IP地址和监听端口号
    std::string _server_ip;
    uint16_t _server_port;
    // 监听器用于监听客户端连接，注册在事件循环的主反应器中
    std::unique_ptr<Acceptor> _acceptor;
    // 保存所有连接的TCP客户端
    std::unordered_map<int, std::shared_ptr<TcpConnection>> _tcpconnections_map; 
    std::mutex _mapmutex; // 保证在多线程安全

protected:
    // 多反应器事件循环(可以被子类继承使用)
    std::shared_ptr<EventLoop> _eventloop;
    // 多反应器事件循环标志
    ReactorModel _reactormodel;
    
protected:
    // 监听器的读回调处理函数，接收客户端的连接并创建TcpConnection或其派生类并保存，然后注册事件循环的子反应器中
    // 根据不同的反应器模式从事件循环中获取一个子反应器添加该Channel事件，在单反应器方式下默认第0个反应器，在多反应器主从方式下按照指定的调度算法，在多反应器对等方式下添加到和acceptor所在的反应器中
    virtual void AcceptReadCallback(int sockfd, std::weak_ptr<EpollTaskScheduler> acceptor_actor);
    
    // 创建套接字sockfd对应的连接器对象TcpConnection，在AcceptReadCallback回调函数中被使用
    // 可被继承重写以方便TcpServer的派生类创建保存TcpConnection的派生类对象
    virtual std::shared_ptr<TcpConnection> CreateClientConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor);

public:
    // 构造函数：由外部传入反应器事件循环，或内部创建反应器事件循环(多反应器时默认采用主从方式)
    TcpServer(std::shared_ptr<EventLoop> eventloop, std::string ip = "0.0.0.0", uint16_t port = 6666);
    TcpServer(int cpus = 0, std::string ip = "0.0.0.0", uint16_t port = 6666);
    virtual ~TcpServer();
    void Close(); // 关闭服务器，清理资源
    // 启动服务器及事件循环,默认采用多反应器主从方式，反应器数量为CPU核心数，监听队列长度为256
    bool Start(TcpServer::ReactorModel model = TcpServer::ReactorModel::MASTER_SLAVE, int blocklog = 256);

    // 添加指定fd的TcpConnection，在acceptor连接客户端成功并进行创建时添加保存
    virtual void AddClientConnection(int sockfd, std::shared_ptr<TcpConnection> tcpconnection);
    // 删除新连接的TcpConnection，设置TcpConnection被关闭时的服务器资源清理逻辑_tspserver_deletecb
    virtual void RemoveClientConnection(int sockfd);

    // 获取服务器IP地址和监听端口号port
    inline std::string GetServerIP() const { return _server_ip; }
    inline uint16_t GetServerPort() const { return _server_port; }
};

#endif