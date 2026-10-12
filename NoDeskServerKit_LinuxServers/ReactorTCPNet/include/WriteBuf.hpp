#ifndef _WRITEBUF_H_
#define _WRITEBUF_H_

#include <cstdint>
#include <memory>
#include <queue>
#include <mutex>
#include <unistd.h>
#include <sys/socket.h>

// 待发送数据包
struct Packet{
    uint32_t _total_len;    // 数据总长度
    uint32_t _sent_len;     // 已发送长度
    std::shared_ptr<char[]> _data;  // 数据缓冲区

    // 构造
    Packet(uint32_t total_len=0, std::shared_ptr<char[]> data=nullptr)
        :_total_len(total_len), _sent_len(0), _data(data) { }
    Packet(uint32_t total_len=0, const char* data=nullptr)
        :_total_len(total_len), _sent_len(0), _data(nullptr) 
    { 
        if(total_len > 0 && data!=nullptr) {
            _data.reset(new char[total_len]);
            memcpy(_data.get(),data,total_len);
        }         
    }

    // 获取判断是否包数据全部发送完毕
    inline bool FinishSend() { return _sent_len == _total_len; }
};

// 发送缓冲区队列
class WriteBuf{
private:
    std::queue<Packet> _sendqueue; // 发送缓冲区队列
    std::mutex _quemutex;   // 锁，保证发送缓冲区每次只能有一个线程发送
    static const uint32_t MAX_QUEUE_PACKET_NUM = 1024 * 100;

public:
    WriteBuf() = default;
    ~WriteBuf() = default;

    // 按照队列顺序添加数据包到发送缓冲区队列
    bool AddSendPacket(uint32_t total_len, std::shared_ptr<char[]> data){
        // 创建Packet数据包并线程安全的添加到队列
        Packet packet(total_len, data);
        // 加锁
        std::lock_guard<std::mutex> lock(_quemutex);
        // 判断是否已满，已满时添加失败
        if(_sendqueue.size() == MAX_QUEUE_PACKET_NUM)
            return false;
        // 未满时添加Packet数据到队尾
        _sendqueue.push(std::move(packet));
        return true;
    }
    bool AddSendPacket(uint32_t total_len, const char* data){
        // 创建Packet数据包并线程安全的添加到队列
        Packet packet(total_len, data);
        // 加锁
        std::lock_guard<std::mutex> lock(_quemutex);
        // 判断是否已满，已满时添加失败
        if(_sendqueue.size() == MAX_QUEUE_PACKET_NUM)
            return false;
        // 未满时添加Packet数据到队尾
        _sendqueue.push(std::move(packet));
        return true;
    }
    bool AddSendPacket(Packet&& packet){
        // 加锁
        std::lock_guard<std::mutex> lock(_quemutex);
        // 判断是否已满，已满时添加失败
        if(_sendqueue.size() == MAX_QUEUE_PACKET_NUM)
            return false;
        // 未满时添加Packet数据到队尾
        _sendqueue.push(std::move(packet));
        return true;
    }

    // 按照队列顺序从发送缓冲区中发送数据
    bool SendSockData(int sockfd){
        // 循环取出队头数据包进行发送直到写满或发送完，保证线程安全。
        std::lock_guard<std::mutex> lock(_quemutex);
        while(_sendqueue.size()>0){
            // 取出队头数据包进行发送，已发送完时不处理直接移除
            Packet& packet = _sendqueue.front();
            if(packet.FinishSend()){
                _sendqueue.pop();
                continue;
            }
            bool flag = true; // 标记本次发送成功标志
            // 循环发送直到写满sock发送缓冲区
            do{
                // 发送数据根据结果处理
                int ret = write(sockfd, packet._data.get() + packet._sent_len, packet._total_len - packet._sent_len);
                if(ret>=0) { // 发送成功
                    packet._sent_len += ret;    // 更新发送长度
                    // 发送完移除
                    if(packet.FinishSend())
                        _sendqueue.pop(); 
                    break;
                }
                else{   // 发送失败
                    // 判断是否写满或者出现致命错误
                    if(errno == EAGAIN){
                        flag = false;
                        break;
                    }else if(errno == EINTR)
                        continue;
                    else{
                        flag = false;
                        break;
                    }
                }    
            }while(true);

            // 判断是否还可以继续发送，写满或出错时不可继续发送
            if(!flag) break;
        }

        // 判断队列的数据包是否发送完
        return _sendqueue.empty();
    }

    // 判断待发送数据包队列是否发送完所有数据包
    bool SendQueueEmpty() {
        std::lock_guard<std::mutex> lock(_quemutex);
        return _sendqueue.empty();
    }
};

#endif