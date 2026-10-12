#ifndef _TIMEREVENTRQUEUE_H_
#define _TIMEREVENTRQUEUE_H_

#include "Timer.hpp"
#include <set>
#include <unordered_map>
#include <map>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

class TimerEventQueue {
private:
    std::atomic<bool> _running;     // 定时器队列是否关闭标志
    std::mutex _timerqueue_mutex;   // 保证多线程环境下定时器队列的安全
    // 哈希Map的key键用于存放定时器ID，第二个value存放定时器智能指针
    std::unordered_map<uint32_t, std::shared_ptr<Timer>> _timer_unordermap;
    // 集合Set的元素为键值对，第一个key键表示超时时间进行排序，第二个为定时器ID，用于查找ID对应的定时器指针
    std::multimap<uint64_t, uint32_t> _timeout_ordermap;

public:
    TimerEventQueue();
    // 析构函数
    ~TimerEventQueue();
    // 关闭定时器队列
    void Close();
    // 定时器队列是否正在运行
    bool Running();

    // 添加定时器任务
    void AddTimer(std::shared_ptr<Timer> timer);
    void AddTimer(uint32_t timerid, bool again, uint64_t timeout, Timer::TimerEventCallback timertask, Timer::TimeOutCallback timeout_callback = nullptr);
    // 移除定时器任务
    void RemoveTimer(std::shared_ptr<Timer> timer);
    void RemoveTimer(uint32_t timerid);
    // 设置或获取指定ID的定时器任务激活执行状态
    void SetTimerActivateState(uint32_t timerid, bool activate);
    int GetTimerActivateState(uint32_t timerid);
    // 获取定时器任务数量
    int GetTimerNumber();
     
    // 定时器任务处理：执行超时定时器任务
    void operator()();
};

#endif
