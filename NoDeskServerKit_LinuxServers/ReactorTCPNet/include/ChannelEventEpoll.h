#ifndef _CHANNELEVENTEPOLL_H_
#define _CHANNELEVENTEPOLL_H_

#include "Channel.hpp"
#include <memory>
#include <unordered_map>
#include <atomic>
#include <mutex>

class ChannelEventEpoll{
protected:
    int _epollfd; // epoll描述符
    // 保存本epoll监听的所有套接字描述符fd及其Channel对象指针
    std::unordered_map<int, std::shared_ptr<Channel>> _channel_map;
    std::mutex _channelmap_mutex; // 保证channel的map操作线程安全
    std::atomic<bool> _running; // 标记是否正在运行

public:
    // 构造函数
    ChannelEventEpoll();
    // 析构函数
    ~ChannelEventEpoll();
    // 关闭epoll监听的Channel事件
    void Close();
    // 获取关闭状态
    bool CloseState();

    // 循环处理epoll中监听就绪的Channel事件并进行处理
    bool operator()();

    // Epoll中添加被监听的Channel的套接字事件，外界需要传入一个设置好的关注事件和处理逻辑的Channel智能指针
    void AddChannel(std::shared_ptr<Channel> channel);
    // Epoll中删除被监听的Channel的套接字事件
    void RemoveChannel(std::shared_ptr<Channel> channel);
    void RemoveChannel(int sockfd);
    // Epoll中更新指定Channel的套接字事件，外界需要传入一个更新好的关注事件和处理逻辑的Channel
    void UpdateChannel(std::shared_ptr<Channel> channel);
    // 获取指定sockfd的Channel对象
    std::shared_ptr<Channel> GetChannel(int sockfd);
    // 获取添加的Channel数量
    int GetChannelNumbers();
};

#endif