#include "TcpPacket.h"
#include <QByteArray>
#include <QDataStream>
#include <QtEndian>

// 构造函数
// 接收到的数据包构造函数
TcpPacket::TcpPacket(std::shared_ptr<char[]> data, unsigned int total_length)
	:message_length(total_length - DATA_ID - CHECK_SUM), checksum(0), need_send(false) {
	// data包括接收到的完整消息内容：消息ID+消息数据+和校验位，total_length表示消息ID+消息数据+和校验位的总长度
	unsigned short id = 0;
	memcpy(&id, data.get(), DATA_ID); // 网络字节序的消息ID
    message_id = static_cast<DataId>(qFromBigEndian(id)); // 转换为主机字节序的消息ID
	message_data.reset(new char[message_length]);
	memcpy(message_data.get(), data.get() + DATA_ID, message_length); // 消息数据本身
	
	// 计算和校验位标记，计算包开始标记0xFEFF+数据总长度total_length+消息ID+消息数据的和校验位
	checksum = 0; // 主机字节序的和校验位
	checksum += 0xFE + 0xFF; 
	unsigned char* ptotal_length = (unsigned char*)&total_length;
	checksum += ptotal_length[0] + ptotal_length[1] + ptotal_length[2] + ptotal_length[3];
	for (unsigned int i = 0; i < total_length - CHECK_SUM; ++i)
        checksum += static_cast<unsigned char>(data[i]);

    // 当主机字节序的计算和校验位等于接收到的网络字节序的和校验位时，数据包有效
    unsigned short receive_sum = 0;
    memcpy(&receive_sum, data.get() + DATA_ID + message_length, CHECK_SUM); // 网络字节序的和校验位
    receive_sum = qFromBigEndian(receive_sum);
    if (checksum == receive_sum)
        validity = true;
    else{
        validity = false;
        std::cerr << "接收的和校验位:" << receive_sum <<",计算的和校验位:" << checksum << "不一致，数据错误。" << std::endl;
    }
}
TcpPacket::TcpPacket(QByteArray data, unsigned int total_length)
    : message_length(total_length - DATA_ID - CHECK_SUM), checksum(0), need_send(false) {
    // data包括接收到的完整消息内容：消息ID+消息数据+和校验位，total_length表示消息ID+消息数据+和校验位的总长度

    // 确保数据长度足够
    if (data.size() < static_cast<unsigned int>(total_length)) {
        qWarning() << "数据长度不足，期望" << total_length << "字节，实际" << data.size() << "字节";
        validity = false;
        return;
    }

    QDataStream stream(&data, QIODevice::ReadOnly);
    stream.setByteOrder(QDataStream::BigEndian);  // 网络字节序，自动解析转为主机字节序

    // 读取消息ID（网络字节序）
    unsigned short id;
    stream >> id;
    message_id = static_cast<DataId>(id);

    // 读取消息数据
    message_data.reset(new char[message_length]);
    stream.readRawData(message_data.get(), message_length);

    // 读取校验和
    unsigned short received_checksum;
    stream >> received_checksum;

    // 计算校验和
    checksum = 0; // 主机字节序的和校验位
    checksum += 0xFE + 0xFF;
    unsigned char* ptotal_length = (unsigned char*)&total_length;
    checksum += ptotal_length[0] + ptotal_length[1] + ptotal_length[2] + ptotal_length[3];
    for (unsigned int i = 0; i < total_length - CHECK_SUM; ++i)
        checksum += static_cast<unsigned char>(data[i]);

    // 验证校验和
    validity = (checksum == received_checksum);

    if (!validity)
        qWarning() << "校验和验证失败，计算值:" << checksum << "接收值:" << received_checksum;
}


// 发送的数据包构造函数
TcpPacket::TcpPacket(DataId id, std::shared_ptr<char[]> data, unsigned int length)
    : message_length(length), message_id(id), message_data(data), need_send(true), sent_data_len(0) {
    // data表示消息数据本身，不包括消息ID和和校验位，length表示消息数据本身长度
	validity = true; // 发送的数据包默认有效
	// 将数据包序列化为网络字节流用于发送，并计算主机字节序的消息ID和消息数据的和校验位
	send_data_buf = tostring();
	send_data_total_len = 2 + DATA_LEN + DATA_ID + message_length + CHECK_SUM;
}

// 获取消息数据包的有效性
bool TcpPacket::is_valid() const {
	return validity;
}
// 获取消息ID
DataId TcpPacket::get_messageid() const {
	return message_id;
}
// 获取消息数据本身
std::shared_ptr<char[]> TcpPacket::get_messagedata() const {
	return message_data;
}
// 获取消息数据长度
unsigned int TcpPacket::get_messagelength() const {
	return message_length;
}

// 获取发送数据本身
std::shared_ptr<char[]> TcpPacket::get_senddata() const {
	return send_data_buf;
}
// 获取发送数据总长度
size_t TcpPacket::get_senddatalen() const {
	return send_data_total_len;
}
// 获取已发送数据长度
size_t& TcpPacket::get_sentsize() {
	return sent_data_len;
}

// 将数据包序列化为网络字节流，用于发送
std::shared_ptr<char[]> TcpPacket::tostring() {
	unsigned int total_length = 2 + DATA_LEN + DATA_ID + message_length + CHECK_SUM; // 构造的整个数据包总长度
	std::shared_ptr<char[]> buffer(new char [total_length] {0}); // 分配总长度的字符串缓冲区
	// 填充包开始标记，采用网络字节序：0xFEFF->FE FF
    unsigned short bn_start_flag = qToBigEndian(start_flag);
    memcpy(buffer.get(),&bn_start_flag,2);
	// 填充数据长度
    unsigned int length = qToBigEndian(DATA_ID + message_length + CHECK_SUM); // 转换为网络字节序
    memcpy(buffer.get()+2,&length,DATA_LEN);
	// 填充消息ID
    unsigned short id = qToBigEndian(static_cast<unsigned short>(message_id)); // 转换为网络字节序
    memcpy(buffer.get()+2+DATA_LEN,&id,DATA_ID);
	// 填充消息数据本身
    memcpy(buffer.get()+2+DATA_LEN+DATA_ID, message_data.get(), message_length);
	// 填充和计算和校验位，包括0xFEFF+数据总长度+消息ID+消息本身
	checksum = 0; // 主机字节序
    unsigned char* buf_ptr = (unsigned char*)buffer.get(); // 转换为无符号字符指针，便于按字节操作
	for (unsigned int i = 0; i < total_length - CHECK_SUM; ++i) 
		checksum += buf_ptr[i];
    unsigned short bn_checksum = qToBigEndian(checksum); // 采用网络字节序保存发送
    memcpy(buffer.get()+2+DATA_LEN+DATA_ID+message_length, &bn_checksum, CHECK_SUM);
	return buffer;
}
