#include "EventLoop.h"
#include <iostream>
// 构造函数
EventLoop::EventLoop(int cpus, ACTORINDEXCALLBACK indexcal_cb)
    : _running(false), _actor_nums(cpus)
    , _actors_index(1), _indexcal_cb(indexcal_cb) // 主从方式默认从第1个反应器开始分配任务，0号反应器作为主反应器负责监听和调度
{
    // 获取要创建的actor数量
    if(cpus == 0)
        _actor_nums = std::max(std::thread::hardware_concurrency() - 1, (unsigned int)1 );
    // 多actor事件进行创建，但不创建线程执行
    for(int i=0;i<_actor_nums;i++) {
        // 创建actor的任务计划
        std::shared_ptr<EpollTaskScheduler> actor_task = std::make_shared<EpollTaskScheduler>(i);
        _actor_taskschedulers.emplace_back(std::move(actor_task));
    }
    // 未显示设置时，使用默认的调度计算方法
    if(!_indexcal_cb)
        // 使用std::bind函数绑定成员函数CalculateActorIndex以std::function<int(EventLoop*)>的函数对象返回
        _indexcal_cb = std::bind(&EventLoop::CalculateActorIndex,std::placeholders::_1);
}

// 事件循环：创建CPU核心数个线程和任务计划，每个线程执行一个任务计划
void EventLoop::Loop() {
    // 多actor事件循环开始，为每个actor创建线程执行
    for(std::shared_ptr<EpollTaskScheduler> actor_task:_actor_taskschedulers)
        // 创建线程
        _actor_threads.emplace_back(std::make_shared<std::thread>([actor_task](){
            (*actor_task)();
        }));
    // 标记运行状态
    _running = true;
#ifdef DEBUG
    // this->AddTimerTask(Timer::generate_id(),true,50000,[](){
    //     std::cout<<"TcpServer定时器测试"<<std::endl;
    // });
#endif
}

// 析构函数
EventLoop::~EventLoop()
{
    // Close() 负责停止所有子反应器并 join 线程，保证析构时无并发访问
    Close();
}

// 停止多反应器事件循环
void EventLoop::Close() {
    // 标记事件循环停止
    if(!_running) return;
    _running = false;

    // 请求每个子反应器尽快退出：先关闭其Channel/Timer并标记停止
    for(std::shared_ptr<EpollTaskScheduler> it : _actor_taskschedulers){
        if(!it) continue;
        it->CloseChannelEvent();
        it->CloseTimerQueue();
        it->Close();
    }

    // 等待所有线程退出并回收资源
    for(std::shared_ptr<std::thread> &th : _actor_threads)
        if(th && th->joinable()) th->join();
    _actor_threads.clear();

    // 所有线程已退出，安全清理子反应器对象
    _actor_taskschedulers.clear();
}

// 默认的子反应器调度算法
int EventLoop::CalculateActorIndex() {
    // 默认的计算方式是在主反应器中创建定时任务：获取当前Channel数量最少的反应器序号
    static std::once_flag once;
    // 只创建一次
    std::call_once(once, [this](){
        // 创建一个定时器任务
        this->AddTimerTask(Timer::generate_id(),true,100,[this](){
            uint32_t min_actorindex = this->GetIndexActorTaskNum(1);
            int actor_index = 1;
            for(int i=2;i<this->GetActorNum();i++){
                int temp = this->GetIndexActorTaskNum(i);
                if(min_actorindex >= temp){
                    min_actorindex = temp;
                    actor_index = i;
                }
            }
            // 在成员函数内部捕获可以使用私有成员，C++ 的访问控制规则是基于代码的位置，而不是基于调用者
            this->_actors_index = actor_index;
#ifdef DEBUG
            std::cerr << "[EventLoop] ActorIndexCallback executed, selected actor index: " << actor_index << " with " << min_actorindex << " channels." << std::endl;
#endif
        });
    });
    
    return _actors_index;
}

// 根据多反应器主从方式的调度算法获取一个当前可用的子反应器
std::shared_ptr<EpollTaskScheduler> EventLoop::GetEnableActor() {
    // 只有一个反应器时，直接使用第一个反应器返回
    if(_actor_nums == 1)
        _actors_index = 0;
    else
        // 执行计算可用子反应器Id的逻辑，内部会设置计算好的保存在_actors_index
        _actors_index = _indexcal_cb(this);
    // 获取该索引对应的子反应器
    return _actor_taskschedulers[_actors_index];
}
    
// 获取指定下标的反应器任务个数
uint32_t EventLoop::GetIndexActorTaskNum(int index) {
    if(index >= _actor_nums)
        return -1; // 越界
    // 返回指定Actor的Channel数量
    return _actor_taskschedulers[index]->GetChannelTaskNumber();
}
    

// 主反应器中分配Timer定时器任务
void EventLoop::AddTimerTask(std::shared_ptr<Timer> timer, int actor_index){
    _actor_taskschedulers[actor_index]->AddTimerTask(timer);
}
void EventLoop::AddTimerTask(uint32_t timerid, bool again, unsigned long timeout, Timer::TimerEventCallback timertask, Timer::TimeOutCallback timeout_callback, int actor_index){
    _actor_taskschedulers[actor_index]->AddTimerTask(timerid, again, timeout, timertask, timeout_callback);
}
// 主反应器中移除定时器任务
void EventLoop::RemoveTimerTask(std::shared_ptr<Timer> timer, int actor_index){
    _actor_taskschedulers[actor_index]->RemoveTimerTask(timer);
}
void EventLoop::RemoveTimerTask(uint32_t timerid, int actor_index){
    _actor_taskschedulers[actor_index]->RemoveTimerTask(timerid);
}

// 主反应器中新增添加Channel套接字事件
void EventLoop::AddChannelTask(std::shared_ptr<Channel> channel, int actor_index){
    _actor_taskschedulers[actor_index]->AddChannelTask(channel);
}
// 主反应器中删除存在的Channel套接字事件
void EventLoop::RemoveChannelTask(std::shared_ptr<Channel> channel, int actor_index){
    _actor_taskschedulers[actor_index]->RemoveChannelTask(channel);
}
void EventLoop::RemoveChannelTask(int sockfd, int actor_index){
    _actor_taskschedulers[actor_index]->RemoveChannelTask(sockfd);
}
// 主反应器中更新存在的Channel套接字事件
void EventLoop::UpdateChannelTask(std::shared_ptr<Channel> channel, int actor_index){
    _actor_taskschedulers[actor_index]->UpdateChannelTask(channel);
}   

// 获取指定索引的反应器任务计划对象
std::shared_ptr<EpollTaskScheduler> EventLoop::GetIndexActor(int index) {
    if(index >= _actor_nums)
        return nullptr; // 越界
    return _actor_taskschedulers[index];
} 
    
    