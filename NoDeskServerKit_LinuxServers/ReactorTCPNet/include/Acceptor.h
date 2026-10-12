#ifndef _ACCEPTOR_H_
#define _ACCEPTOR_H_

#include <unistd.h>
#include <functional>
#include "EventLoop.h"
#include "TcpServer.h"

// 用于服务器进行监听
class Acceptor{
public:
    using TcpServerAcceptCallback = std::function<void(int, std::weak_ptr<EpollTaskScheduler>)>;

protected:
    std::string _ip;        // 监听IP地址
    uint16_t _port;         // 监听port端口号
    int _sockfd;            // 监听套接字
    // TcpServer设置的读事件回调，用于监听客户端的连接
    TcpServerAcceptCallback _accept_callback;

    // 创建监听套接字，并设置监听套接字属性，-1表示错误，大于0表示成功创建的套接字
    int Socket();
    // 绑定地址
    int Bind();
    // 监听
    int Listen(int blocklog = 256);
    // 读就绪事件即接收到客户端连接的处理逻辑，内部会为链接成功的客户端sockfd调用_accept_callback在服务器进行创建
    void AcceptReadCallback(std::weak_ptr<EpollTaskScheduler> actor); // actor为监听器的Channel事件所在的反应器，方便在多反应器对等方式中获取本acceptor所在的反应器进行添加Channel事件

public:
    Acceptor(std::string ip = "0.0.0.0", uint16_t port = 6666);
    ~Acceptor();
    // 关闭监听，释放资源(在主反应器中移除添加的Actor的Channel,最后关闭监听套接字)
    void Close();
    // 开始接受客户端的连接，创建监听套接字的Channel对象，将其放在事件循环的主反应器中
    bool Accept(std::shared_ptr<EventLoop> eventloop, TcpServer::ReactorModel model = TcpServer::ReactorModel::MASTER_SLAVE, int blocklog = 256);

    // 设置连接成功后的sockfd在服务器中处理逻辑
    inline void SetAcceptReadCallback(TcpServerAcceptCallback accept_callback) { _accept_callback = accept_callback; }
    
    // 获取IP地址和port端口号
    inline std::string GetServerIP() const { return _ip; }
    inline uint16_t GetServerPort() const { return _port; }
};

#endif