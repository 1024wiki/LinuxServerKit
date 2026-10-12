#include "SigServer.h"
#include "SigConnection.h"
#include <thread>

// 静态成员类外初始化
std::shared_ptr<SigServer> SigServer::instance = nullptr;

// 构造函数
SigServer::SigServer(std::shared_ptr<EventLoop> eventloop, std::string ip, uint16_t port)
        :TcpServer(eventloop, ip, port) {}
SigServer::SigServer(int cpus, std::string ip, uint16_t port)
        :TcpServer(cpus, ip, port) {}

// 析构函数
SigServer::~SigServer(){}

// 获取静态单例
std::shared_ptr<SigServer> SigServer::GetInstances(int cpus, std::string ip, uint16_t port){
    static std::once_flag once; // 声明为静态保证只被初始化一次，std::call_once只执行一次
    std::call_once(once, [cpus, ip, port](){
        // instance是类静态成员变量，类似于全局变量不需要捕获，可以直接访问使用
        SigServer::instance.reset(new SigServer(cpus, ip, port));
    });
    return SigServer::instance;
}
    
// 重写在TcpServer中连接客户端成功后创建的客户端连接器为TcpConnection的派生类SigConnection
std::shared_ptr<TcpConnection> SigServer::CreateClientConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor){
    return std::make_shared<SigConnection>(sockfd, actor);
}
