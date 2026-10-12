#ifndef _TIMER_H_
#define _TIMER_H_

#include <limits.h>
#include <functional>
#include <chrono>
#include <random>

class Timer{
public:
    using TimerEventCallback = std::function<void()>; // 定时器任务类型
    // 超时时间计算逻辑，第一个为开始时间戳、第二个为超时时间周期
    using TimeOutCallback = std::function<uint64_t(uint64_t, uint64_t)>;  

public:
    // 构造函数
    // 默认构造函数，需要手动设置相关属性，默认定时器不启动
    Timer(uint32_t timerid = 0)
        : _timerid(timerid), _activate(false)
        , _again(false), _timeoutval(ULONG_MAX)
        , _timertask([](){}) 
    {
        // 获取当前时间戳作为起始时间
        _start_time = GetNowTimeStamp();
        // 设置超时时间计算函数，未提供时采用默认的超时计算函数
        _timeout_callback = &Timer::DefaultTimeoutCallback;
    }
    // 采用自己传入的定时器计算逻辑，需传入定时器需要的所有属性
    Timer(uint32_t timerid, bool again, uint64_t timeout, TimerEventCallback time_task, TimeOutCallback timeout_callback = nullptr)
        : _timerid(timerid), _activate(true)
        , _again(again), _timeoutval(timeout)
        , _timertask(time_task), _timeout_callback(timeout_callback)
    {
        // 获取当前时间戳作为起始时间
        _start_time = GetNowTimeStamp();
        if(timeout_callback == nullptr)
            // 设置超时时间计算函数，未提供时采用默认的超时计算函数
            _timeout_callback = &Timer::DefaultTimeoutCallback;
    }
    // 析构函数
    virtual ~Timer() = default;

    // 获取定时器ID
    inline uint32_t GetTimerID() const { return _timerid; }
    // 设置和获取定时器是否生效
    inline void SetActivateState(bool activate) { _activate = activate; }
    inline bool GetActivateState() const { return _activate; }
    // 设置和获取任务是否反复执行
    inline bool GetAgainState() const { return _again; }
    inline void SetAgainState(bool again){ _again = again; }
    // 设置和获取超时时间间隔
    inline void SetTimeout(uint64_t timeout) { _timeoutval = timeout; }
    inline uint64_t GetTimeout() const { return _timeoutval; }
    // 获取启动时间戳
    inline uint64_t GetStartTime() const { return _start_time; }

    // 设置超时任务，支持函数模板重载方式
    inline void SetTimeTask(TimerEventCallback timetask){ _timertask = timetask; }
    template<typename _FUNC_, typename... _ARGS_> // 不定量参数在定义时，...在类型名后；在使用时，...在值后面。
    void SetTimeTask(_FUNC_&& func, _ARGS_&&... args){ 
        _timertask = std::bind(std::forward<_FUNC_>(func), std::forward<_ARGS_>(args)...); 
    }
    
    // 设置超时时间计算逻辑
    inline void SetTimeoutCallback(TimeOutCallback timeoutcallback){ _timeout_callback = timeoutcallback; }
    // 根据当前时间获取下个最佳超时时间，设置为虚函数保证在继承时可以由外界设置超时时间计算方式
    virtual uint64_t NextTimeout() const {
        return _timeout_callback(_start_time, _timeoutval);
    }

    // 函数对象(仿函数)：在定时器激活时，执行超时任务；未激活时不处理
    void operator()() const { if(_activate) _timertask(); }

    // 获取当前时间的时间戳(毫秒)
    static uint64_t GetNowTimeStamp(){
        // 获取当前时间点
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(); 
        // 将当前时间点转化为毫秒数的时间周期
        std::chrono::microseconds now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        // 返回这个时间周期的毫秒数即时间戳
        return now_ms.count();
    }

    // 获取一个32位无符号整数
    static uint32_t generate_id() {
        static thread_local std::mt19937 generator(std::random_device{}());
        return generator();
    }
protected:
    // 默认的超时时间计算逻辑
    static uint64_t DefaultTimeoutCallback(uint64_t start_time, uint64_t timeoutval) {
        // 获取计算当前时间的时间戳
        uint64_t timestamp = GetNowTimeStamp();
        // 计算下个最佳超时时间
        uint64_t timeval = timestamp - start_time; // 时间差
        // 根据时间差计算定时器下个周期匹配的超时时间
        uint64_t nexttimeout = start_time + (timeval / timeoutval + 1) * timeoutval;
        return nexttimeout;
    }

private:
    uint32_t _timerid; // 定时器ID
    bool _activate; // 设置定时器是否生效，不生效时不执行
    bool _again; // 是否循环执行
    uint64_t _start_time; // 任务开始时间
    uint64_t _timeoutval; // 超时时间间隔(毫秒)
    TimerEventCallback _timertask; // 超时任务
    TimeOutCallback _timeout_callback; // 超时时间计算函数(传入开始时间戳和超时时间计算下次超时时间)
};

#endif