#include "Acceptor.h"
#include "Channel.hpp"
#include "SockTool.h"
#include <sys/socket.h>
#include <string>
#include <arpa/inet.h>
#include <iostream>

Acceptor::Acceptor(std::string ip, uint16_t port)
    : _ip(ip), _port(port), _sockfd(-1) 
{
    // 创建监听套接字
    if(Socket() == -1) return;
    // 绑定IP地址和端口号port
    if(Bind() == -1) return;
    
}
Acceptor::~Acceptor() { Close(); }

// 关闭监听，释放资源(在主反应器中移除添加的Actor的Channel,最后关闭监听套接字)
void Acceptor::Close(){
    if(_sockfd == -1) return;
    // 关闭监听套接字
    int tmp = _sockfd;
    _sockfd = -1;
    ::close(tmp);
}
 
// 开始接受客户端的连接，创建监听套接字的Channel对象，将其放在事件循环的反应器中
bool Acceptor::Accept(std::shared_ptr<EventLoop> eventloop, TcpServer::ReactorModel model, int blocklog){
    // 监听套接字创建失败直接返回
    if(_sockfd == -1) return false;
    // 开始进行监听
    if(Listen(blocklog) == -1) return false;
    
    // 关键添加到事件循环的反应器中
    switch(model){
        // 单反应器方式直接添加到事件循环的唯一反应器中
        case TcpServer::ReactorModel::SINGLE:{
            // 创建accept的Channel，并注册读事件和对应回调函数进行连接客户端
            std::shared_ptr<Channel> acceptchannel = std::make_shared<Channel>(_sockfd,0,true);
            // 获取事件循环的唯一反应器
            std::shared_ptr<EpollTaskScheduler> actor = eventloop->GetIndexActor(0);
            if(actor) {
                // 注册读事件监听并设置读事件回调函数
                acceptchannel->AddReadEvent();
                std::weak_ptr<EpollTaskScheduler> weak_actor = actor; // 传递弱指针以避免循环引用
                acceptchannel->SetReadEventCallback(&Acceptor::AcceptReadCallback,this,weak_actor);
                // 添加到事件循环的唯一反应器中
                actor->AddChannelTask(acceptchannel);
            }
            break;
        }
        // 主从方式添加到事件循环的主反应器中
        case TcpServer::ReactorModel::MASTER_SLAVE:{
            // 创建accept的Channel，并注册读事件和对应回调函数进行连接客户端
            std::shared_ptr<Channel> acceptchannel = std::make_shared<Channel>(_sockfd,0,true);
            
            // 添加到默认第0个反应器为主反应器
            std::shared_ptr<EpollTaskScheduler> actor = eventloop->GetIndexActor(0);
            if(actor) {
                // 注册读事件监听并设置读事件回调函数
                acceptchannel->AddReadEvent(); 
                std::weak_ptr<EpollTaskScheduler> weak_actor = actor; // 传递弱指针以避免循环引用
                acceptchannel->SetReadEventCallback(&Acceptor::AcceptReadCallback,this,weak_actor);
                // 添加到事件循环的主反应器中
                actor->AddChannelTask(acceptchannel);
            }
            break;
        }
        // 多反应器对端方式添加到事件循环的所有反应器中
        case TcpServer::ReactorModel::PEER_TO_PEER:{
            // 每个反应器都添加一个accept的Channel，并注册读事件和对应回调函数进行连接客户端
            for(int i=0; i<eventloop->GetActorNum(); ++i){
                std::shared_ptr<Channel> acceptchannel = std::make_shared<Channel>(_sockfd,0,true);
                // 添加到事件循环的指定索引的第i个反应器中
                std::shared_ptr<EpollTaskScheduler> actor = eventloop->GetIndexActor(i);
                if(actor) {
                    // 注册读事件监听并设置读事件回调函数
                    acceptchannel->AddReadEvent(true); // 多反应器对等方式下添加EPOLLEXCLUSIVE以避免惊群效应
                    std::weak_ptr<EpollTaskScheduler> weak_actor = actor; // 传递弱指针以避免循环引用
                    acceptchannel->SetReadEventCallback(std::bind(&Acceptor::AcceptReadCallback,this,weak_actor));
                    // 添加到事件循环的指定索引的第i个反应器中
                    actor->AddChannelTask(acceptchannel);
                }
            }
            break;
        }
        default:{
             std::cerr << "[Acceptor] Invalid ReactorModel specified." << std::endl;
            return false;
        }
           
    };
    return true;
}

// 创建监听套接字，并设置监听套接字属性，-1表示错误，大于0表示成功创建的套接字
int Acceptor::Socket(){
    // 创建监听套接字
    int ret = ::socket(AF_INET,SOCK_STREAM,0); 
    if(ret == -1) {
        _sockfd = -1;
#ifdef DEBUG
        std::cerr << "[Acceptor] Socket creation failed: " << strerror(errno) << std::endl;
#endif  
        return -1;
    }
    else 
        _sockfd = ret;
    // 设置监听套接字sockfd属性为地址可重用
    if(!SockTool::SetSockaddrReuse(_sockfd)) {
        ::close(_sockfd);
        _sockfd = -1;
#ifdef DEBUG
        std::cerr << "[Acceptor] SetSockaddrReuse failed: " << strerror(errno) << std::endl;
#endif
        return -1;
    }
    // 设置监听套接字非阻塞属性
    SockTool::SetNonBlock(_sockfd);
    return ret;
}

// 绑定地址
int Acceptor::Bind(){
    if(_sockfd == -1) return -1;

    // 绑定IP地址和端口号port
    struct sockaddr_in sockaddr; 
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = inet_addr(_ip.data()); // 将点分十进制的IP字符串转化为32无符号整形
    sockaddr.sin_port = htons(_port); // 网络协议需要转化为网络字节序
    int ret = ::bind(_sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
    // 绑定失败，关闭套接字并重置sockfd为-1
    if(ret == -1) {
        ::close(_sockfd);
        _sockfd = -1;
#ifdef DEBUG
        std::cerr << "[Acceptor] Bind failed: " << strerror(errno) << std::endl;
#endif
    }
    return ret;
}

// 监听
int Acceptor::Listen(int blocklog){
    if(_sockfd == -1) return -1;
    int ret = ::listen(_sockfd, blocklog);
    if(ret == -1) {
        ::close(_sockfd);
        _sockfd = -1;
#ifdef DEBUG
        std::cerr << "[Acceptor] Listen failed: " << strerror(errno) << std::endl;
#endif
        return -1;
    }
    std::cout<<"listening("<<_ip<<":"<<_port<<")........"<<std::endl;
    return ret;
}

// 读就绪事件即接收到客户端连接的处理逻辑
void Acceptor::AcceptReadCallback(std::weak_ptr<EpollTaskScheduler> actor){
    std::cout<<"[Acceptor] AcceptReadCallback: ready to accept new client connections..."<<std::endl;   
    // 采用ET方式循环接收客户端连接，为每个连接成功的客户端调用回调函数_accept_callback在TcpServer中创建客户端
    while(true){
        // 接收客户端的连接
        int sockfd = ::accept(_sockfd, NULL, NULL);
        if(sockfd == -1){
            // 出错，判断是否为缓冲区为空或者被中断、其他致命错误
            if(errno == EAGAIN)
                return; // 正常结束
            else if(errno == EINTR)
                continue; // 被打断再次监听客户端连接
            else
                return; // 其他致命错误，异常结束                
        }
        else
            // 接收客户端连接成功，调用TcpServer的回调函数创建TcpConnection
            _accept_callback(sockfd, actor);
    }
}

