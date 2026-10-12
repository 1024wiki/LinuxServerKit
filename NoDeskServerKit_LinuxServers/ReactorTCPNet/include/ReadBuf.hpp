#ifndef _READBUF_H_
#define _READBUF_H_

#include <vector>
#include <string>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <errno.h>

// 读缓冲区：专门用于从sockfd中读取网络数据进行保存，并提供自动扩容机制
class ReadBuf
{
private:
    std::vector<char> _rdbuf; // 保存数据   
    uint32_t _startpos;    // 记录读缓冲区的可用数据的起始位置，每次都指向第一个已存放但未读取的缓冲区起始位置
    uint32_t _endpos;      // 记录读缓冲区的可以数据的最后位置+1，每次都指向第一个空闲的缓冲区起始位置
    uint32_t _capacity;    // 记录读缓冲区的数据容量总大小
    static const uint32_t MAX_PER_READ_DATASIZE = 4096; // 每次循环读取的最大数据长度
    static const uint32_t MAX_READ_BUFSIZE = 1024 * 1024 * 10; // 缓冲区最大10M

    // 扩容逻辑：类似于std::vector的扩容机制，一般进行扩容就不缩容以避免频繁的内存碎片
    inline void Capacity(){
        // 每次扩容原容量的2倍，直到增长到MAX_READ_BUFSIZE
        uint32_t new_capacity = std::min(2 * _capacity, (uint32_t)MAX_READ_BUFSIZE);
        _rdbuf.reserve(new_capacity);
        _capacity = new_capacity;
    }
public:
    ReadBuf(int capacity = 4 * MAX_PER_READ_DATASIZE)
        : _capacity(capacity), _startpos(0), _endpos(0)
    {
        // 分配缓冲区实际大小，确保 data() 可用且写入安全
        _rdbuf.reserve(capacity);
    }   
    ~ReadBuf() = default;

    // 获取当前可用数据长度，可用数据长度为0时说明无数据，可以重置数据起始指针
    inline uint32_t ReadableDataSize(){ 
        if(_startpos>=_endpos){
            _startpos = 0;
            _endpos = 0;
            return 0;
        }else
            return _endpos-_startpos;
    }
    // 获取当前可写数据长度，可写数据长度不满足一个MAX_PER_READ_DATASIZE时，先进行前移已用数据长度
    // 然后继续判断可写数据长度不满足一个MAX_PER_READ_DATASIZE时，进行扩容2倍原容量，然后返回可用大小
    uint32_t WriteableDataSize(){ 
        // 当前可写长度
        uint32_t write_size = _endpos>=_capacity?0:(_capacity - _endpos);
        // 不够时先进行前移已用数据长度
        if(write_size < MAX_PER_READ_DATASIZE){
            // 前移已用长度
            uint32_t data_size = ReadableDataSize(); // 当前未使用数据长度
            if(data_size > 0)
                memmove(_rdbuf.data(), ReadStartPos(), data_size); // memmove会处理内存重叠
            // 更新数据起止位置
            _startpos = 0;
            _endpos = data_size;
            // 判断是否已够大小，不够时进行扩容
            write_size = _endpos>=_capacity?0:(_capacity - _endpos);
            if(write_size < MAX_PER_READ_DATASIZE){
                Capacity(); // 进行扩容2倍
                write_size = _endpos>=_capacity?0:(_capacity - _endpos);
            }
        }

        return write_size;
    }

    // 获取可读数据的起始字符位置，用之前需要先判断是否有数据可读
    inline char* ReadStartPos(){ return _rdbuf.data() + _startpos; }

    // 获取可写数据的起始字符位置，nullptr表示数据已写满
    inline char* WriteStartPos(){ return _rdbuf.data() + _endpos; }

    // 更新数据起始位置(可读位置)
    inline void OffsetStartPos(uint32_t offset){ _startpos += offset; }
    // 更新数据最后位置(可写位置)
    inline void OffsetEndPos(uint32_t offset){ _endpos += offset; }

    // 将缓冲区内所有数据读出变成一个字符串
    std::string ReadAllData(){
        std::string data(ReadStartPos(), ReadableDataSize());
        // 更新数据起止位置，全部读出时重置为初始状态
        _startpos = 0;
        _endpos = 0;
        return data;
    }

    // 从sockfd中读取数据到缓冲区中，采用ET方式循环读取，返回本次循环读取的数据长度(-1表示出错)
    long ReadSockData(int sockfd){
        long total_read = 0; // 本次循环读取的数据长度
        int ret = 0;    // 本次循环读取返回值
        bool flag = true;    // 本次循环读取成功标志
        // 循环读取数据到读缓冲区，直到返回-1并且errno为EAGAIN
        do{
            // 先判断读缓冲区是否有空间useable_size，进行扩容
            // 循环每次最多读取MAX_PER_READ_DATASIZE大小
            uint32_t can_read =  std::min((uint32_t)MAX_PER_READ_DATASIZE, WriteableDataSize());
            if(can_read == 0)
                return total_read; // 无空间到达最大空间，直接返回已读长度
            // 读缓冲区有剩余空间，进行读取数据并根据返回值判断结果
            ret = read(sockfd, WriteStartPos(), can_read);
            if(ret>0) {   // 成功
                OffsetEndPos(ret);  // 更新数据起始位置
                total_read += ret;   
            }else if(ret == 0)   // 对端断开，表示发送数据完毕
                break;
            else{   // 失败
                // 判断是循环读取结束，还是出现致命错误
                if(errno == EAGAIN) // 读完缓冲区数据
                    break;
                else if(errno == EINTR) // 被中断下次继续读
                    continue;
                else{   // 出现致命错误
                    flag = false;
                    break;
                }
            }
        }while(true);

        // 成功返回循环读取的数据长度，失败返回-1
        return flag?total_read:ret;
    }
};


#endif