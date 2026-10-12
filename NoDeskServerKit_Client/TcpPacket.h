#pragma once
#include "global.h"

// Tcp数据包类：数据包格式=>包开始标记0xFEFF+数据长度4字节+数据内容+和校验位2字节
class TcpPacket
{
private:
	const unsigned short start_flag = 0xFEFF; // 包开始标记
    unsigned int message_length; // 消息本身长度，不包含消息ID和和校验位长度
	DataId message_id; // 消息ID，规定发送/接收的网络数据为网络字节序
	std::shared_ptr<char[]> message_data; // 消息数据本身，规定发送/接收的网络数据为网络字节序
	unsigned short checksum; // 和校验位，规定发送/接收为网络字节序，并计算包头0xFEFF+数据总长度+消息ID+消息本身
	bool validity; // 数据包有效性标志

	// 需要发送Tcp数据包时使用的成员变量
	bool need_send;        // 是否需要发送数据
	std::shared_ptr<char[]> send_data_buf;        // 发送缓冲区
	size_t send_data_total_len;       // 待发送数据总长度
	size_t sent_data_len;       // 已发送数据长度

	// 将数据包序列化为网络字节流，用于发送
	std::shared_ptr<char[]> tostring();
	
public:
	// 接收到的数据包构造函数
	TcpPacket(std::shared_ptr<char[]> data, unsigned int total_length);
    TcpPacket(QByteArray data, unsigned int total_length);

	// 发送的数据包构造函数
	TcpPacket(DataId id, std::shared_ptr<char[]> data, unsigned int length);

	// 析构函数
	~TcpPacket() = default;

	// 获取消息数据包的有效性
	bool is_valid() const;
	// 获取消息ID
	DataId get_messageid() const;
	// 获取消息数据本身
	std::shared_ptr<char[]> get_messagedata() const;
	// 获取消息数据长度
	unsigned int get_messagelength() const;

	// 获取发送数据本身
	std::shared_ptr<char[]> get_senddata() const;
	// 获取发送数据总长度
	size_t get_senddatalen() const;
	// 获取已发送数据长度
	size_t& get_sentsize();
};

