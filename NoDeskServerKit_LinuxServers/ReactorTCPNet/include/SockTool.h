#ifndef _SOCKTOOL_H_
#define _SOCKTOOL_H_

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <fcntl.h>

// Socket工具类
class SockTool
{
public:
    // 设置非阻塞属性
    static void SetNonBlock(int sockfd){
        // 添加非阻塞属性
        int flag = fcntl(sockfd, F_GETFL, 0);
        flag |= O_NONBLOCK;
        fcntl(sockfd, F_SETFL, flag);
    }

    // 设置阻塞属性
    static bool SetBlock(int sockfd){
        int flag = fcntl(sockfd, F_GETFL, 0);
        flag &= ~O_NONBLOCK;
        int ret = fcntl(sockfd, F_SETFL, flag);
        return (ret>=0)?true:false;
    }

    // 设置地址重用、端口重用
    static bool SetSockaddrReuse(int sockfd){
        int flag = 1;
        int ret = 0;
        // 设置地址重用
        ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
        // 设置端口重用
        ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));
        return (ret==0)?true:false;
    }

    // 设置发送缓冲区大小
    static bool SetSendBufferSize(int sockfd, int bufsize){
        int ret = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
        return (ret==0)?true:false;
    }

    // 设置接收缓冲区大小
    static bool SetRecvBufferSize(int sockfd, int bufsize){
        int ret = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
        return (ret==0)?true:false;
    }

    // 设置保持连接属性
    static bool SetKeepTcpLive(int sockfd){
        int flag = 1;
        int ret = setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
        return (ret==0)?true:false;
    }

    // 设置禁用Nalay算法，减少缓冲以降低延迟，但会增加网络负载
    static bool SetNoNagle(int sockfd){
        int flag = 1;
        int ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        return (ret==0)?true:false;
    }

    // 设置采用立即回应ACK包，以提高发送接收效率，但会增加网络负载
    static bool SetQuickACK(int sockfd){
        int flag = 1;
        int ret = setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));
        return (ret==0)?true:false;
    }
};

#endif