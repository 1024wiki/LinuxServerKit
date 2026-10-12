#include "TaskScheduler.h"

 // 构造函数
TaskScheduler::TaskScheduler() 
    :_running(true) {}

// 析构函数
TaskScheduler::~TaskScheduler() { 
    Close(); 
} 

// 关闭任务计划器，包括内部的定时器队列
void TaskScheduler::Close() { 
    _running = false;
}

// 关闭定时器队列
void TaskScheduler::CloseTimerQueue() {  
    _timerqueue.Close(); 
}

// 循环执行任务计划中的任务，包括定时器队列中的定时任务，使用虚函数保证可以被继承重写添加其他任务
void TaskScheduler::operator()(){
    // 任务计划正在运行
    while(_running)
        _timerqueue(); // 处理定时器队列中的定时任务
}

// 添加定时器任务
void TaskScheduler::AddTimerTask(std::shared_ptr<Timer> timer) { 
    if(_running) 
        _timerqueue.AddTimer(timer); 
}
void TaskScheduler::AddTimerTask(uint32_t timerid, bool again, uint64_t timeout, Timer::TimerEventCallback timertask, Timer::TimeOutCallback timeout_callback) {
    if(_running) 
        _timerqueue.AddTimer(timerid, again, timeout, timertask, timeout_callback); 
}

// 移除指定ID的定时器任务
void TaskScheduler::RemoveTimerTask(std::shared_ptr<Timer> timer) { 
    if(_running) 
        _timerqueue.RemoveTimer(timer); 
}
void TaskScheduler::RemoveTimerTask(uint32_t timerid) { 
    if(_running) 
        _timerqueue.RemoveTimer(timerid); 
}

// 设置或获取指定ID的定时器任务激活执行状态
void TaskScheduler::SetTimerTaskActivateState(uint32_t timerid, bool activate) { 
    if(_running) 
        _timerqueue.SetTimerActivateState(timerid, activate); 
}
bool TaskScheduler::GetTimerTaskActivateState(uint32_t timerid) { 
    return _running?_timerqueue.GetTimerActivateState(timerid):false; 
}

// 获取定时器队列中的定时器数量
int TaskScheduler::GetTimerTaskNumber(){
    return _timerqueue.GetTimerNumber();
}