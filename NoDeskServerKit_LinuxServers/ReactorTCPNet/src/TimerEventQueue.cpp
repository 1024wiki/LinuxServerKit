#include "TimerEventQueue.h"
#include <iostream>

TimerEventQueue::TimerEventQueue() :_running(true) {}

TimerEventQueue::~TimerEventQueue() {
    // 请求停止
    Close();
    // 加锁保证线程安全的释放
    std::lock_guard<std::mutex> lock(_timerqueue_mutex);
    _timer_unordermap.clear();
    _timeout_ordermap.clear();
}

void TimerEventQueue::Close(){
    _running = false;
}

bool TimerEventQueue::Running(){
    return _running;
}

void TimerEventQueue::AddTimer(std::shared_ptr<Timer> timer)
{
    if(timer == nullptr) return;
    // 定时器关闭直接返回
    if(!_running) return;
    // 加锁保证多线程安全
    std::lock_guard<std::mutex> lock(_timerqueue_mutex);
    // 再次检查
    if(!_running) return;

    // 在定时器队列中添加新的定时器任务
    uint32_t timerid = timer->GetTimerID();
    _timer_unordermap.emplace(timerid, timer);
    _timeout_ordermap.emplace(timer->NextTimeout(), timerid);
#ifdef DEBUG
    std::cerr << "[TimerEventQueue] AddTimer id=" << timerid << " next=" << timer->NextTimeout()
              << " map_size=" << _timer_unordermap.size() << " order_size=" << _timeout_ordermap.size() << std::endl;
#endif
}

void TimerEventQueue::AddTimer(uint32_t timerid, bool again, uint64_t timeout, Timer::TimerEventCallback timertask, Timer::TimeOutCallback timeout_callback)
{
    // 创建定时器对象
    std::shared_ptr<Timer> timer = std::make_shared<Timer>(timerid, again, timeout, timertask, timeout_callback);
    
    // 定时器关闭直接返回
    if(!_running) return;
    // 加锁保证多线程安全
    std::lock_guard<std::mutex> lock(_timerqueue_mutex);
    // 再次检查
    if(!_running) return;
    
    // 在定时器队列中添加新的定时器任务
    std::pair<uint64_t, uint32_t> timeout_timer(timer->NextTimeout(), timerid);
    _timeout_ordermap.insert(timeout_timer);
    _timer_unordermap.emplace(timerid, timer);
#ifdef DEBUG
    std::cerr << "[TimerEventQueue] AddTimer id=" << timerid << " next=" << timer->NextTimeout()
              << " map_size=" << _timer_unordermap.size() << " order_size=" << _timeout_ordermap.size() << std::endl;
#endif
}

void TimerEventQueue::RemoveTimer(std::shared_ptr<Timer> timer)
{
    if(timer == nullptr) return;
    // 定时器关闭直接返回
    if(!_running) return;
    // 加锁保证多线程安全
    std::lock_guard<std::mutex> lock(_timerqueue_mutex);
    // 再次检查
    if(!_running) return;

    // 在哈希map中移除，在set中不移除，直到要被执行时才移除
    _timer_unordermap.erase(timer->GetTimerID());
#ifdef DEBUG
    std::cerr << "[TimerEventQueue] RemoveTimer id=" << timer->GetTimerID()
              << " map_size=" << _timer_unordermap.size() << " order_size=" << _timeout_ordermap.size() << std::endl;
#endif
}

void TimerEventQueue::RemoveTimer(uint32_t timerid)
{
    /// 定时器关闭直接返回
    if(!_running) return;
    // 加锁保证多线程安全
    std::lock_guard<std::mutex> lock(_timerqueue_mutex);
    // 再次检查
    if(!_running) return;

    // 在哈希map中移除，在set中不移除，直到要被执行时才移除
    _timer_unordermap.erase(timerid);
#ifdef DEBUG
    std::cerr << "[TimerEventQueue] RemoveTimer id=" << timerid
              << " map_size=" << _timer_unordermap.size() << " order_size=" << _timeout_ordermap.size() << std::endl;
#endif
}

void TimerEventQueue::SetTimerActivateState(uint32_t timerid, bool activate){
    // 定时器关闭直接返回
    if(!_running) return;
    // 加锁保证多线程安全
    std::lock_guard<std::mutex> lock(_timerqueue_mutex);
    // 再次检查
    if(!_running) return;

    // 先找到定时器任务队列中的定时器，在更改激活状态
    std::unordered_map<uint32_t, std::shared_ptr<Timer>>::iterator it = _timer_unordermap.find(timerid);
    if(it!=_timer_unordermap.end())
        it->second->SetActivateState(activate);
}

int TimerEventQueue::GetTimerActivateState(uint32_t timerid){
    // 定时器关闭直接返回
    if(!_running) return -1;
    // 加锁保证多线程安全
    std::lock_guard<std::mutex> lock(_timerqueue_mutex);
    // 再次检查
    if(!_running) return -1;

    // 先找到定时器任务队列中的定时器，在更改激活状态
    std::unordered_map<uint32_t, std::shared_ptr<Timer>>::iterator it = _timer_unordermap.find(timerid);
    if(it!=_timer_unordermap.end())
        return it->second->GetActivateState();
    else
        return -1;
}

int TimerEventQueue::GetTimerNumber(){
    // 定时器关闭直接返回
    if(!_running) return -1;
    // 加锁保证多线程安全
    std::lock_guard<std::mutex> lock(_timerqueue_mutex);
    // 再次检查
    if(!_running) return -1;

    return _timer_unordermap.size();
}

void TimerEventQueue::operator()()
{
    // 按照超时时间顺序执行set中所有超时任务，直到所有任务执行完毕，或者所有任务均未超时
    while(_running) {
        // 加锁并再次检查
        std::unique_lock<std::mutex> lock(_timerqueue_mutex);
        if(!_running) return;
        
        // 定时器队列为空直接返回
        if(_timeout_ordermap.empty() || _timer_unordermap.empty()) {
#ifdef DEBUG
            // std::cerr << "[TimerEventQueue] No timers to process. Waiting..." << std::endl;
            // std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 避免空轮询过快占用CPU
#endif
            return;
        }
        // 从第一个最小超时时间的定时器任务进行处理
        std::pair<uint64_t, uint32_t> timeout_timer = *_timeout_ordermap.begin(); // 获取当前超时定时器的超时时间和ID
        if(timeout_timer.first > Timer::GetNowTimeStamp()) return;
        // 执行第一个超时任务取出，并判断是否需要反复执行就再次计算下次超时时间进行插入
        uint32_t timerid = _timeout_ordermap.begin()->second; // 获取当前超时定时器ID
        _timeout_ordermap.erase(_timeout_ordermap.begin());
        // 根据当前超时定时器ID，查找定时器指针，没找到说明被删除不处理
        std::unordered_map<uint32_t, std::shared_ptr<Timer>>::iterator it = _timer_unordermap.find(timerid);
        if(it==_timer_unordermap.end()) continue;
        std::shared_ptr<Timer> timer = it->second;
        // 可以解锁
        lock.unlock();

        // 找到就执行，并在激活执行后，判断是否需要反复执行，再次插入到Set集合中
        (*timer)(); // 执行

        // 需要加锁
        lock.lock();
        if(!_running) return;
        // 判断是否需要反复触发执行，重新计算超时时间进行再次构建pair插入到set中
        if(timer->GetAgainState()&&_timer_unordermap.count(timerid)>0)
            _timeout_ordermap.emplace(timer->NextTimeout(), timerid);
        // 不需要再次执行时就进行在哈希map中删除智能指针，并且不在插入到set中
        else
            _timer_unordermap.erase(timerid);
        
#ifdef DEBUG
        std::cerr << "[TimerEventQueue] Processing timer id=" << timerid << " next=" << timer->NextTimeout()
                  << " map_size=" << _timer_unordermap.size() << " order_size=" << _timeout_ordermap.size() << std::endl;
#endif
        // 自动解锁
    }
}
