#ifndef _TASKSCHEDULER_H_
#define _TASKSCHEDULER_H_

#include <atomic>
#include <memory>
#include "TimerEventQueue.h"

// 任务计划器用于在一个反应器中循环执行定时任务
class TaskScheduler{
protected:
    std::atomic<bool> _running; // 是否正在执行
    TimerEventQueue _timerqueue; // 定时器任务队列
    
public:
    // 构造函数
    TaskScheduler();
    // 析构函数
    virtual ~TaskScheduler(); 
    // 关闭任务计划器，包括内部的定时器队列
    void Close();
    // 关闭定时器队列
    void CloseTimerQueue();

    // 循环执行任务计划中的任务，包括定时器队列中的定时任务，使用虚函数保证可以被继承重写添加其他任务
    virtual void operator()();
    
    // 添加定时器任务
    void AddTimerTask(std::shared_ptr<Timer> timer);
    void AddTimerTask(uint32_t timerid, bool again, uint64_t timeout, Timer::TimerEventCallback timertask, Timer::TimeOutCallback timeout_callback = nullptr);
    
    // 移除指定ID的定时器任务
    void RemoveTimerTask(std::shared_ptr<Timer> timer);
    void RemoveTimerTask(uint32_t timerid);

    // 设置或获取指定ID的定时器任务激活执行状态
    void SetTimerTaskActivateState(uint32_t timerid, bool activate);
    bool GetTimerTaskActivateState(uint32_t timerid);
    // 获取定时器队列中的定时器数量
    int GetTimerTaskNumber();
};

#endif