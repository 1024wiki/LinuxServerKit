#ifndef _EPOLLTASKSCHEDULER_H_
#define _EPOLLTASKSCHEDULER_H_

#include "TaskScheduler.h"
#include "ChannelEventEpoll.h"
#include <memory>

// 除继承的定时器任务功能外，还添加Channel的IO事件的Epoll任务计划器
class EpollTaskScheduler: public TaskScheduler {
protected:
    int _actor_index; // 任务计划所在反应器的索引，方便在多反应器模式下进行调度分配
    // epoll任务IO事件    
    ChannelEventEpoll _channelevent;
    
public:
    // 构造函数
    EpollTaskScheduler(int actor_index = 0);
    // 析构函数：父类使用自己的虚析构函数进行资源释放，子类只需保证自己新创建的资源释放逻辑
    virtual ~EpollTaskScheduler();
    // 关闭处理函数
    void CloseChannelEvent();

    // 重写的父类TaskScheduler的事件处理逻辑
    virtual void operator()() override;

    // 定时器任务管理通过继承TaskScheduler获得，补充Channel套接字事件管理函数
    // 新增添加Channel套接字事件
    void AddChannelTask(std::shared_ptr<Channel> channel);
    // 删除存在的Channel套接字事件
    void RemoveChannelTask(std::shared_ptr<Channel> channel);
    void RemoveChannelTask(int sockfd);
    // 更新存在的Channel套接字事件
    void UpdateChannelTask(std::shared_ptr<Channel> channel);
    // 获取指定套接字sockfd的Channel对象
    std::shared_ptr<Channel> GetChannelTask(int sockfd);
    // 获取Channel数量
    int GetChannelTaskNumber();
};


#endif