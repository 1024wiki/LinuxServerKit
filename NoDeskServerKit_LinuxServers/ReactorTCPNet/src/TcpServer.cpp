#include "TcpServer.h"
#include "Acceptor.h"
#include <iostream>

TcpServer::TcpServer(std::shared_ptr<EventLoop> eventloop, std::string ip, uint16_t port)
    : _running(true), _eventloop(eventloop)
    , _server_ip(ip), _server_port(port)
    , _acceptor(std::make_unique<Acceptor>(ip, port))
{
    // 关键是在这里设置监听器接收客户端连接成功后的Acceptor在TcpServer的服务器创建TcpConnection对象
    _acceptor->SetAcceptReadCallback(std::bind(&TcpServer::AcceptReadCallback,this,std::placeholders::_1,std::placeholders::_2));
}
TcpServer::TcpServer(int cpus, std::string ip, uint16_t port)
    : _running(true), _eventloop(std::make_shared<EventLoop>(cpus))
    , _server_ip(ip), _server_port(port)
    , _acceptor(std::make_unique<Acceptor>(ip, port))
{
    // 关键是在这里设置监听器接收客户端连接成功后的Acceptor在TcpServer的服务器创建TcpConnection对象
    _acceptor->SetAcceptReadCallback(std::bind(&TcpServer::AcceptReadCallback,this,std::placeholders::_1,std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    Close();
}

void TcpServer::Close(){
    // 关闭服务器
    if(!_running) return;
    _running = false;
    // 关闭多反应器事件循环
    if(_eventloop) _eventloop->Close();
    // 关闭监听器的监听套接字
    _acceptor->Close();
}

// 启动服务器及事件循环
bool TcpServer::Start(TcpServer::ReactorModel model, int blocklog){
    // 按照指定方式启动服务器并开始事件循环，在反应器数量为1时退化到单反应器方式
    if(!_acceptor->Accept(_eventloop, model ,blocklog)) 
        return false; // 开启进行监听客户端连接
    _reactormodel = model; // 保存服务器启动的反应器模式
    // 开始多反应器的事件循环
    _eventloop->Loop(); 
    return true;
}

// 可被继承重写以方便TcpServer的派生类创建保存TcpConnection的派生类对象
std::shared_ptr<TcpConnection> TcpServer::CreateClientConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor){
    return std::make_shared<TcpConnection>(sockfd, actor);
}

// 监听器的读回调处理函数，接收客户端的连接并创建TcpConnection并保存，然后注册事件循环的子反应器中
void TcpServer::AcceptReadCallback(int sockfd, std::weak_ptr<EpollTaskScheduler> acceptor_actor){
    // 服务器关闭直接返回
    if(!_running) return;

    // 根据服务器启动的反应器模式，从事件循环中获取一个子反应器添加该Channel事件
    // 在单反应器方式下，新连接的客户端Channel事件默认添加到事件循环的第0个反应器中
    // 在多反应器主从方式下，新连接的客户端Channel事件按照指定的调度算法添加到事件循环的指定分配的子反应器中
    // 在多反应器对等方式下，新链接的客户端Channel事件添加到和acceptor所在的反应器中
    std::shared_ptr<EpollTaskScheduler> actor = nullptr;
    switch (_reactormodel)
    {
    case TcpServer::ReactorModel::SINGLE:
        // 单反应器方式直接添加到事件循环的唯一反应器中，默认为第0个反应器
        actor = _eventloop->GetIndexActor(0);
        break;
    case TcpServer::ReactorModel::MASTER_SLAVE:
        // 主从方式添加到按照指定调度算法进行获取的事件循环的子反应器中
        actor = _eventloop->GetEnableActor();
        break;
    case TcpServer::ReactorModel::PEER_TO_PEER:
        // 多反应器对端方式添加到和acceptor的Channel所在的反应器中
        actor = acceptor_actor.lock();
        break;
    default:
        // 其他未知方式直接返回
        std::cerr << "[TcpServer] AcceptReadCallback unknown ReactorModel" << std::endl;
        return;
    }

    // 该函数可以被重写，采用多态方式创建TcpConnection的派生类对象，并将其添加到指定反应器中
    std::shared_ptr<TcpConnection> tcpconnection = CreateClientConnection(sockfd, actor);
    // 设置TCP客户端的服务器资源清理逻辑
    tcpconnection->SetTcpServerDeleteCallback(std::bind(&TcpServer::RemoveClientConnection,this,std::placeholders::_1));
    
    // TcpServer服务器添加保存该连接客户端
    AddClientConnection(sockfd, tcpconnection);
}

// 添加指定fd的TcpConnection，在acceptor连接客户端成功并进行创建时添加保存
void TcpServer::AddClientConnection(int sockfd, std::shared_ptr<TcpConnection> tcpconnection){
    if(!_running) return;
    // 加锁添加
    std::lock_guard<std::mutex> lock(_mapmutex);
    if(!_running) return;
    _tcpconnections_map.emplace(sockfd, tcpconnection);
#ifdef DEBUG
    std::cerr << "[TcpServer] AddClientConnection fd=" << sockfd << " total=" << _tcpconnections_map.size() << std::endl;
#endif
}
// 删除新连接的TcpConnection，设置TcpConnection被关闭时的服务器资源清理逻辑_tspserver_deletecb
void TcpServer::RemoveClientConnection(int sockfd){
    if(!_running) return;
    // 加锁删除
    std::lock_guard<std::mutex> lock(_mapmutex);
    if(!_running) return;
    _tcpconnections_map.erase(sockfd);
#ifdef DEBUG
    std::cerr << "[TcpServer] RemoveClientConnection fd=" << sockfd << " total=" << _tcpconnections_map.size() << std::endl;
#endif
}