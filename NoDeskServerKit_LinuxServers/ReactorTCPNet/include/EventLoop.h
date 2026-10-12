#ifndef _EVENTLOOP_H_
#define _EVENTLOOP_H_

#include "EpollTaskScheduler.h"
#include <thread>
#include <mutex>
#include <vector>
#include <memory>
#include <atomic>
#include <unordered_map>

// 多反应器模型事件循环或单反应器模型事件循环
// ①单反应器方式：只有一个反应器即复杂监听也负责事件处理，所有事件均在同一个反应器中进行事件循环执行
// ②多反应器主从方式：第0个反应器被当成主反应器负责事件监听并分发，子反应器调度、关键定时器任务等关键任务
//  其他反应器被当成子反应器只进行所分配任务的处理：管理自身的定时器任务和所分配的Channel任务。
// ③多反应器对等方式：每个反应器既负责事件监听和分发，也负责所分配的事件处理，适用于多核CPU资源较少的情况
class EventLoop {
public:
    // 子反应器调度算法逻辑
    using ACTORINDEXCALLBACK = std::function<int(EventLoop*)>;

private:
    std::atomic<bool> _running; // 多反应器事件循环运行状态
    
    // 每个任务计划被看做一个反应器，采用一个线程执行
    std::vector<std::shared_ptr<EpollTaskScheduler>> _actor_taskschedulers;  // 反应器任务计划
    std::vector<std::shared_ptr<std::thread>> _actor_threads;    // 反应器线程
    int _actor_nums; // 反应器线程数量
    // 多反应器主从方式下的子反应器调度分配任务逻辑，单反应器模式默认使用第0个，多反应器对等方式由外界指定添加到acceptor所在的反应器或指定的反应器中
    std::atomic<int> _actors_index; // 保存多反应器主从方式下的当前可被分配任务的反应器下标
    ACTORINDEXCALLBACK _indexcal_cb; // 多反应器主从方式下的子反应器调度算法逻辑
    
protected:
    // 默认的多反应器主从方式下的子反应器调度算法逻辑
    int CalculateActorIndex();

public:
    // 构造函数
    EventLoop(int cpus = 0, ACTORINDEXCALLBACK indexcal_cb = nullptr);
    // 析构函数
    virtual ~EventLoop();
    
    // 停止多反应器事件循环
    void Close();
    // 事件循环：创建CPU核心数个线程和任务计划，每个线程执行一个任务计划
    void Loop();

    // 设置多反应器主从方式的调度算法
    inline void SetActorIndexCallback(ACTORINDEXCALLBACK indexcal_cb){ _indexcal_cb = indexcal_cb; }
    // 根据多反应器主从方式的调度算法获取一个当前可用的子反应器
    std::shared_ptr<EpollTaskScheduler> GetEnableActor();

    // 获取反应器个数
    inline int GetActorNum() { return _actor_nums; }
    // 获取指定下标的反应器任务个数
    uint32_t GetIndexActorTaskNum(int index);

    // 在指定反应器中添加和移除Timer定时器任务，在多反应器的主从方式下默认第0个反应器为主反应器中添加任务，在多反应器的对等方式下由外界指定添加到哪个反应器中
    void AddTimerTask(std::shared_ptr<Timer> timer, int actor_index = 0);
    void AddTimerTask(uint32_t timerid, bool again, unsigned long timeout, Timer::TimerEventCallback timertask, Timer::TimeOutCallback timeout_callback = nullptr, int actor_index = 0);
    void RemoveTimerTask(std::shared_ptr<Timer> timer, int actor_index = 0);
    void RemoveTimerTask(uint32_t timerid, int actor_index = 0);

    // 在指定反应器中添加和移除Channel通道任务，在多反应器的主从方式下默认第0个反应器为主反应器中添加任务，在多反应器的对等方式下由外界指定添加到哪个反应器中
    void AddChannelTask(std::shared_ptr<Channel> channel, int actor_index = 0);
    void RemoveChannelTask(std::shared_ptr<Channel> channel, int actor_index = 0);
    void RemoveChannelTask(int sockfd, int actor_index = 0);
    void UpdateChannelTask(std::shared_ptr<Channel> channel, int actor_index = 0);

    // 获取指定索引的反应器任务计划对象
    std::shared_ptr<EpollTaskScheduler> GetIndexActor(int index);
};

#endif