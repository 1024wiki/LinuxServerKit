#include "EpollTaskScheduler.h"
#include <iostream>

EpollTaskScheduler::EpollTaskScheduler(int actor_index)
    : _actor_index(actor_index), TaskScheduler() {}

EpollTaskScheduler::~EpollTaskScheduler()
{
    CloseChannelEvent();
}

void EpollTaskScheduler::CloseChannelEvent()
{
    // 关闭子类自身资源
    _channelevent.Close();
}

void EpollTaskScheduler::operator()()
{
    // 重写父类任务计划，在循环执行定时器任务的基础上，新增Channel任务处理
    while(_running) {
        // 定时器队列的函数对象执行定时器的超时任务处理逻辑
        _timerqueue();
        // Channel事件Epoll函数对象执行Epoll事件的Channel任务处理逻辑
        _channelevent();
#ifdef DEBUG
        std::cerr << "[EpollTaskScheduler:" << _actor_index << "] Looping with " << GetTimerTaskNumber() << " timers and " << GetChannelTaskNumber() << " channels." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 调试时每秒输出一次状态，避免过多日志干扰
#endif
    }
}

void EpollTaskScheduler::AddChannelTask(std::shared_ptr<Channel> channel)
{
    if(_running) _channelevent.AddChannel(channel);
}

void EpollTaskScheduler::RemoveChannelTask(std::shared_ptr<Channel> channel)
{
    if(_running) _channelevent.RemoveChannel(channel);
}

void EpollTaskScheduler::RemoveChannelTask(int sockfd)
{
    if(_running) _channelevent.RemoveChannel(sockfd);
}

void EpollTaskScheduler::UpdateChannelTask(std::shared_ptr<Channel> channel)
{
    if(_running) _channelevent.UpdateChannel(channel);
}

std::shared_ptr<Channel> EpollTaskScheduler::GetChannelTask(int sockfd)
{
    return _running?_channelevent.GetChannel(sockfd):nullptr;
}

int EpollTaskScheduler::GetChannelTaskNumber(){
    return _channelevent.GetChannelNumbers();
}