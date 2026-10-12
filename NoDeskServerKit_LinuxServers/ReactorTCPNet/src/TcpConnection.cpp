#include "TcpConnection.h"

TcpConnection::TcpConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> taskscheduler)
    : _close(false), _actor(taskscheduler)
{
    // 创建Channel并设置监听事件和相应处理函数
    _channel = std::make_shared<Channel>(sockfd, 0, true);

    // 设置关键属性
    // 设置禁用Nagle算法，以减少延迟
    SockTool::SetNoNagle(sockfd);
    // 设置立即ACK确认方式，不采用默认的捎带确认方式，以提高发送效率，减少延迟
    SockTool::SetQuickACK(sockfd);
    // 设置发送缓冲区大小
    SockTool::SetSendBufferSize(sockfd, 100 * 1024);
    // 设置保持TCP心跳
    SockTool::SetKeepTcpLive(sockfd);

    // 设置监听事件和处理的回调函数
    _channel->AddReadEvent(); // 先注册读事件
    _channel->AddPeerCloseEvent(); // 添加对端关闭写端事件的半关闭监听
    // channel读回调处理
    _channel->SetReadEventCallback(std::bind(&TcpConnection::HandleReadEventCallback, this));
    // channel写回调处理
    _channel->SetWriteEventCallback(std::bind(&TcpConnection::HandleWriteEventCallback, this));
    // channel断连回调处理
    _channel->SetCloseEventCallback(std::bind(&TcpConnection::HandleCloseEventCallback, this));
    // channel错误回调处理
    _channel->SetErrorEventCallback(std::bind(&TcpConnection::HandleErrorEventCallback, this));

    // 在反应器中添加该Channel任务
    std::shared_ptr<EpollTaskScheduler> actor = _actor.lock();
    if(actor) actor->AddChannelTask(_channel);
}

TcpConnection::~TcpConnection()
{
    CloseTcpConnection();
}

// 关闭连接，并释放相关资源(actor保存的Channel删除、上层业务关闭逻辑、tcpserver清理tcp连接、关闭文件描述符 )
void TcpConnection::CloseTcpConnection(){
    if(_close) return;
    _close = true;
    // 先清理所在actor反应器保存的Channel资源
    std::shared_ptr<EpollTaskScheduler> actor = _actor.lock();
    if(actor) actor->RemoveChannelTask(_channel);
    // 判断上层业务层是否设置关闭时的处理逻辑
    if(_close_tcpcb_dealcb)
        _close_tcpcb_dealcb(shared_from_this());
    // Tcp服务器资源清理逻辑
    if(_tcpserver_deletecb)
        _tcpserver_deletecb(GetSockFD());
    // 关闭套接字
    _channel->Close();
}

// 相应的处理逻辑
void TcpConnection::HandleReadEventCallback(){
    // 先读完sock内核接收缓冲区到用户接收缓冲区，然后调用上层业务层的处理函数进行读取的用户接收缓冲区数据
    long recv_size = _recvbuf.ReadSockData(GetSockFD());
    if(recv_size < 0) // 出错时关闭连接，并释放相关资源(actor保存的Channel、上层业务关闭逻辑、tcpserver保存的Connection、Channel的套接字)
        CloseTcpConnection();
    // 上层业务逻辑不为空时进行相应处理
    if(_read_tcpcb_dealcb)
        _read_tcpcb_dealcb(_recvbuf);
}    
void TcpConnection::HandleWriteEventCallback(){
    // 发送在待发送缓冲区中的队列数据包，没发送完时更新Channel注册写事件回调，等待下次Channel写事件就绪时由反应器处理继续发送4
    bool finish = _sendbuf.SendSockData(GetSockFD());
    if(finish) 
        if(_channel->StateWriteEvent()) {
            _channel->RemoveWriteEvent(); // 发送完时取消写事件注册
            // 更新Channel事件到反应器中，避免不必要的写事件触发
            std::shared_ptr<EpollTaskScheduler> actor = _actor.lock();
            if(actor) actor->UpdateChannelTask(_channel);
        }
    else
        if(!_channel->StateWriteEvent()) {
            _channel->AddWriteEvent(); // 没发送完时增加写事件注册
            // 更新Channel事件到反应器中，等待下次写事件触发时继续发送
            std::shared_ptr<EpollTaskScheduler> actor = _actor.lock();
            if(actor) actor->UpdateChannelTask(_channel);
        }
}    
void TcpConnection::HandleCloseEventCallback(){
    CloseTcpConnection();
}  
void TcpConnection::HandleErrorEventCallback(){
    CloseTcpConnection(); // 这里简化错误事件处理直接关闭连接，并释放相关资源
}


// Sock数据发送
void TcpConnection::Send(Packet&& packet){
    // 添加数据包到待发送缓冲区队列中，并立即进行一次发送
    _sendbuf.AddSendPacket(std::move(packet));
    // 发送数据包
    HandleWriteEventCallback();
}
void TcpConnection::Send(uint32_t total_len, std::shared_ptr<char[]> data){
    // 添加数据包到待发送缓冲区队列中，并立即进行一次发送
    _sendbuf.AddSendPacket(total_len, data);
    // 发送数据包
    HandleWriteEventCallback();
}
void TcpConnection::Send(uint32_t total_len, const char* data){
    // 添加数据包到待发送缓冲区队列中，并立即进行一次发送
    _sendbuf.AddSendPacket(total_len, data);
    // 发送数据包
    HandleWriteEventCallback();
}