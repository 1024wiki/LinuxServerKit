#ifndef _SIGSERVER_H_
#define _SIGSERVER_H_

#include "TcpServer.h"

// 创建单例方式的信令服务器
class SigServer: public TcpServer{
private:
    static std::shared_ptr<SigServer> instance;
    // 构造函数
    SigServer(std::shared_ptr<EventLoop> eventloop, std::string ip = "0.0.0.0", uint16_t port = (uint16_t)10086U);
    SigServer(int cpus = 0, std::string ip = "0.0.0.0", uint16_t port = 10086);

public:
    // 析构函数
    ~SigServer();

    // 获取静态单例
    static std::shared_ptr<SigServer> GetInstances(int cpus = 0, std::string ip = "0.0.0.0", uint16_t port = 10086);
    
    // 重写在TcpServer中连接客户端成功后创建的客户端连接器为TcpConnection的派生类SigConnection
    std::shared_ptr<TcpConnection> CreateClientConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor) override;
};

#endif