#include "ChannelEventEpoll.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>

ChannelEventEpoll::ChannelEventEpoll()
    :_running(true)
{
    _epollfd = epoll_create(100);
}

ChannelEventEpoll::~ChannelEventEpoll()
{
    Close();
    // 关闭epoll监听的Channel事件，清除保存的Channel智能指针和epoll注册的套接字及事件
    std::unique_lock<std::mutex> lock(_channelmap_mutex);
    _channel_map.clear();
    int temp = _epollfd;
    _epollfd=-1;
    lock.unlock();
    // 关闭epollfd
    close(temp);
}

bool ChannelEventEpoll::CloseState(){
    if(_running==false||_epollfd == -1)
        return true;
    else
        return false;
}

void ChannelEventEpoll::Close(){
    _running = false;
}

void ChannelEventEpoll::AddChannel(std::shared_ptr<Channel> channel)
{
    if(channel == nullptr) return; // 空不处理
    // 检查是否已经关闭或出错
    if(CloseState()) return;
    
    int sockfd = channel->GetSockFD();
    // 需要加锁保证线程安全，主反应器可能会在子反应器中添加新连接的Channel
    std::lock_guard<std::mutex> lock(_channelmap_mutex);
    // 再次检查是否停止运行
    if(CloseState()) return;

    // 保存Channel
    _channel_map.emplace(sockfd, channel);
    // epoll中新增Channel事件
    struct epoll_event epoll_events;
    epoll_events.events = channel->GetEpollEvent();
    epoll_events.data.fd = sockfd; // 使用 fd，在处理时再从 map 取 shared_ptr
    epoll_ctl(_epollfd, EPOLL_CTL_ADD, sockfd, &epoll_events);
#ifdef DEBUG
    std::cerr << "[ChannelEventEpoll] AddChannel fd=" << sockfd << " events=" << epoll_events.events << std::endl;
#endif
}

void ChannelEventEpoll::RemoveChannel(std::shared_ptr<Channel> channel)
{
    if(channel == nullptr) return; // 空不处理
    // 检查是否已经关闭或出错
    if(CloseState()) return;

    int sockfd = channel->GetSockFD();
    // 需要加锁保证线程安全，主反应器可能会在子反应器中添加新连接的Channel
    std::lock_guard<std::mutex> lock(_channelmap_mutex);
    // 再次检查是否停止运行
    if(CloseState()) return;

    // 清除保存的Channel智能指针和epoll注册的套接字及事件
    _channel_map.erase(sockfd);
    epoll_ctl(_epollfd,EPOLL_CTL_DEL,sockfd,NULL);
#ifdef DEBUG
    std::cerr << "[ChannelEventEpoll] RemoveChannel fd=" << sockfd << std::endl;
#endif
}

void ChannelEventEpoll::RemoveChannel(int sockfd)
{
    // 检查是否已经关闭或出错
    if(CloseState()) return;
    
    // 需要加锁保证线程安全，主反应器可能会在子反应器中添加新连接的Channel
    std::lock_guard<std::mutex> lock(_channelmap_mutex);
    // 再次检查是否停止运行
    if(CloseState()) return;

    // 清除保存的Channel智能指针和epoll注册的套接字及事件
    _channel_map.erase(sockfd);
    epoll_ctl(_epollfd,EPOLL_CTL_DEL,sockfd,NULL);
#ifdef DEBUG
    std::cerr << "[ChannelEventEpoll] RemoveChannel fd=" << sockfd << std::endl;
#endif
}

void ChannelEventEpoll::UpdateChannel(std::shared_ptr<Channel> channel)
{
    if(channel == nullptr) return; // 空不处理
    // 检查是否已经关闭或出错
    if(CloseState()) return;

    // 更新epoll中已经存在的Channel对象
    int sockfd = channel->GetSockFD();

    // 需要加锁保证线程安全，主反应器可能会在子反应器中添加新连接的Channel
    std::lock_guard<std::mutex> lock(_channelmap_mutex);
    // 再次检查是否停止运行
    if(CloseState()) return;

    // 先判断是否存在该sockfd对应的Channel对象，不存在时不处理
    if(_channel_map.find(sockfd)==_channel_map.end())
        return;
    else{
        // 存在就进行更新
        struct epoll_event epoll_events;
        epoll_events.data.fd = sockfd;
        epoll_events.events = channel->GetEpollEvent();
        epoll_ctl(_epollfd,EPOLL_CTL_MOD,sockfd,&epoll_events);
#ifdef DEBUG
        std::cerr << "[ChannelEventEpoll] UpdateChannel fd=" << sockfd << " events=" << epoll_events.events << std::endl;
#endif
    }
}

std::shared_ptr<Channel> ChannelEventEpoll::GetChannel(int sockfd){
    // 检查是否已经关闭或出错
    if(CloseState()) return nullptr;
    
    // 需要加锁保证线程安全，主反应器可能会在子反应器中添加新连接的Channel
    std::lock_guard<std::mutex> lock(_channelmap_mutex);
    // 再次检查是否停止运行
    if(CloseState()) return nullptr;

    // 获取指定sockfd对应的Channel指针
    std::unordered_map<int,std::shared_ptr<Channel>>::iterator it = _channel_map.find(sockfd);
    if(it==_channel_map.end())
        return nullptr;
    else
        return it->second;
}

int ChannelEventEpoll::GetChannelNumbers(){
    // 检查是否已经关闭或出错
    if(CloseState()) return -1;
    
    // 需要加锁保证线程安全，主反应器可能会在子反应器中添加新连接的Channel
    std::lock_guard<std::mutex> lock(_channelmap_mutex);
    // 再次检查是否停止运行
    if(CloseState()) return -1;

    return _channel_map.size();
}

bool ChannelEventEpoll::operator()()
{
    // 出错或已经关闭，不再继续执行epoll事件
    if(CloseState()) return false;

    // 进行epoll事件监听并采用Channel设置好的事件回调处理函数进行处理
    struct epoll_event ready_events[128];
    int nums = epoll_wait(_epollfd, ready_events, 128, 300); 
    if(nums==-1) {
        // 非致命错误，下次还可以正常执行
        if(errno==EAGAIN||errno==EINTR)
            return true;

        // 出现致命错误直接关闭epoll，不继续处理
        Close();
        return false;
    } else if(nums==0)
        return true;
    else {
        // 处理就绪的epoll文件描述符事件
        for(int i=0;i<nums;i++){
            int fd = ready_events[i].data.fd;
            uint32_t events = ready_events[i].events; // 获取就绪事件，这里会被系统修改传出就绪epoll事件
            // 从map获取 shared_ptr（在锁内获取副本以保证对象不被销毁），然后在锁外调用回调
            std::shared_ptr<Channel> channelptr = nullptr;
            {
                std::lock_guard<std::mutex> lk(_channelmap_mutex);
                auto it = _channel_map.find(fd);
                if(it==_channel_map.end()) continue;
                channelptr = it->second; // 拷贝 shared_ptr
            }
            if(channelptr) {
#ifdef DEBUG
                std::cerr << "[ChannelEventEpoll] Processing fd=" << fd << " events=" << events << std::endl;
#endif
                (*channelptr)(events);
            }
        }
        return true;
    }

}