#ifndef _CHANNEL_H_
#define _CHANNEL_H_

#include <sys/epoll.h>
#include <functional>
#include "SockTool.h"
#include <unistd.h>

class Channel {
public:
    using SocketEventCallback = std::function<void()>; // socket套接字的epoll事件处理回调函数

public:
    // 构造函数
    Channel(int sockfd, uint32_t epollevent = 0, bool et = false, SocketEventCallback readcb=[](){}, SocketEventCallback writecb=[](){}, SocketEventCallback errorcb=[](){}, SocketEventCallback closecb=[](){})
        : _sockfd(sockfd), _epoll_event(epollevent), _edgetouch(et)
        , _readcallback(readcb)
        , _writecallback(writecb)
        , _errorcallback(errorcb)
        , _closecallback(closecb) 
    {
        // 采用边缘触发时需要设置套接字为非阻塞方式
        if(_edgetouch) SockTool::SetNonBlock(sockfd);
    }
    // 析构函数
    ~Channel(){ Close(); }
    // 关闭套接字
    void Close() { ::close(_sockfd); }

    // 获取读事件的监听状态
    inline bool StateReadEvent() { return _epoll_event & EPOLLIN; }
    // 获取写事件的监听状态
    inline bool StateWriteEvent(){ return _epoll_event & EPOLLOUT; }
    // 获取套接字是否有事件监听
    inline bool StateNoEvent() { return _epoll_event==0; }

    // 设置使用ET方式或LT方式
    inline void SetEdgeLevelTouch(bool flag) { _edgetouch = flag; }
    // 获取使用ET方式或LT方式
    inline bool GetTouchType() { return _edgetouch; }
    // 获取套接字描述符
    inline int GetSockFD() { return _sockfd; }
    // 获取套接字监听事件
    inline uint32_t GetEpollEvent() { return _epoll_event; }

    // 注册读事件监听
    inline void AddReadEvent(bool exclusive = false) {
        // 是否采用ET边缘触发方式或默认的LT水平触发方式
        if(_edgetouch) _epoll_event |= EPOLLET; 
        else _epoll_event &= ~EPOLLET;
        // 是否可能会被多进程/线程同时使用，如果是则需要添加EPOLLEXCLUSIVE以避免惊群效应
        if(exclusive) _epoll_event |= EPOLLEXCLUSIVE;
        _epoll_event |= EPOLLIN;  
    }
    // 取消读事件监听
    inline void RemoveReadEvent() { _epoll_event &= ~EPOLLIN; }
    // 设置读事件回调函数，支持函数模板方式
    void SetReadEventCallback(SocketEventCallback readcb) { _readcallback = readcb; }
    template<typename _FUNC_, typename... _ARGS_>
    void SetReadEventCallback(_FUNC_&& func, _ARGS_&&... args){
        _readcallback = std::bind(std::forward<_FUNC_>(func), std::forward<_ARGS_>(args)...);
    }

    // 注册写事件监听
    inline void AddWriteEvent(bool exclusive = false) { 
        // 是否采用ET边缘触发方式或默认的LT水平触发方式
        if(_edgetouch) _epoll_event |= EPOLLET;
        else _epoll_event &= ~EPOLLET;
        // 是否可能会被多进程/线程同时使用，如果是则需要添加EPOLLEXCLUSIVE以避免惊群效应
        if(exclusive) _epoll_event |= EPOLLEXCLUSIVE;
        _epoll_event |= EPOLLOUT;
    }
    // 取消写事件监听
    inline void RemoveWriteEvent(){ _epoll_event &= ~EPOLLOUT; }
    // 设置写事件回调函数，支持函数模板方式
    inline void SetWriteEventCallback(SocketEventCallback writecb) { _writecallback = writecb; }
    template<typename _FUNC_, typename... _ARGS_>
    void SetWriteEventCallback(_FUNC_&& func, _ARGS_&&... args){
        _writecallback = std::bind(std::forward<_FUNC_>(func), std::forward<_ARGS_>(args)...);
    }

    // 添加对端关闭写端事件的半关闭监听
    inline void AddPeerCloseEvent(bool exclusive = false) { 
        // 是否采用ET边缘触发方式或默认的LT水平触发方式
        if(_edgetouch) _epoll_event |= EPOLLET;
        else _epoll_event &= ~EPOLLET;
        // 是否可能会被多进程/线程同时使用，如果是则需要添加EPOLLEXCLUSIVE以避免惊群效应
        if(exclusive) _epoll_event |= EPOLLEXCLUSIVE;
        _epoll_event |= EPOLLRDHUP; 
    }
    // 取消对端关闭写端事件的半关闭监听
    inline void RemovePeerCloseEvent() { _epoll_event &= ~EPOLLRDHUP; }

    // 设置错误事件回调函数，支持函数模板方式
    inline void SetErrorEventCallback(SocketEventCallback errorcb) { _errorcallback = errorcb; }
    template<typename _FUNC_, typename... _ARGS_>
    void SetErrorEventCallback(_FUNC_&& func, _ARGS_&&... args){
        _errorcallback = std::bind(std::forward<_FUNC_>(func), std::forward<_ARGS_>(args)...);
    }

    // 设置关闭事件回调函数，支持函数模板方式
    inline void SetCloseEventCallback(SocketEventCallback closecb) { _closecallback = closecb; }
    template<typename _FUNC_, typename... _ARGS_>
    void SetCloseEventCallback(_FUNC_&& func, _ARGS_&&... args){
        _closecallback = std::bind(std::forward<_FUNC_>(func), std::forward<_ARGS_>(args)...);
    }

    
    // 处理Channel通道的套接字就绪事件
    // 注意事件处理顺序：错误事件 > 连接不可用事件 > 对端关闭写端事件 > 带外数据事件 > 普通可读事件 > 可写事件
    void operator()(uint32_t events) {
        // 错误事件优先处理，继续使用无意义
        if (events & EPOLLERR) {
            _errorcallback();
            _closecallback();   // 错误必须关
            return;
        }

        // 连接已不可用（全关闭）
        if (events & EPOLLHUP) {
            // 仍然尝试读残余数据（重要！）
            if (events & EPOLLIN)
                _readcallback();
            // 在进行关闭
            _closecallback();
            return;
        }

        // 对端关闭写端（半关闭，最常见）
        if (events & EPOLLRDHUP) {
            // 必须读完剩余数据
            if (events & EPOLLIN)
                _readcallback();
            // 然后进行关闭
            _closecallback();
            return;
        }

        // 带外数据（单独处理）
        if (events & EPOLLPRI) _readcallback();  
        // 普通可读
        if (events & EPOLLIN) _readcallback();
        // 可写（注意：不要常驻监听）
        if (events & EPOLLOUT) _writecallback();
    }

private:
    int _sockfd; // 通信套接字
    uint32_t _epoll_event; // 感兴趣的epoll事件
    bool _edgetouch;    // 事件触发方式是否采用ET方式
    SocketEventCallback _readcallback;  // 读事件处理回调函数
    SocketEventCallback _writecallback; // 写事件处理回调函数
    SocketEventCallback _errorcallback; // 错误事件处理回调函数
    SocketEventCallback _closecallback; // 关闭事件处理回调函数
};

#endif