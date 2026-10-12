#ifndef DEFINE_H_
#define DEFINE_H_

#include <cstdint>
#include <string>
#include <cstring>
#include <memory>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

// ==================== 信令帧协议常量 ====================
// 帧格式: StartFlag(2) + DataLen(4, BE) + MsgId(2, BE) + Body(N) + Checksum(2, BE)
constexpr uint8_t  HEAD_FLAG_FIRST  = 0xFE;
constexpr uint8_t  HEAD_FLAG_SECOND = 0xFF;
constexpr uint32_t START_FLAG_LEN   = 2;
constexpr uint32_t DATA_LEN_FIELD   = 4;   // DataLen 字段字节数
constexpr uint32_t MSG_ID_LEN       = 2;
constexpr uint32_t CHECKSUM_LEN     = 2;
constexpr uint32_t FRAME_HEAD_LEN   = START_FLAG_LEN + DATA_LEN_FIELD;          // 6
constexpr uint32_t FRAME_TAIL_LEN   = MSG_ID_LEN + CHECKSUM_LEN;                 // body 之外固定 4
constexpr uint32_t FRAME_MIN_LEN    = START_FLAG_LEN + DATA_LEN_FIELD + MSG_ID_LEN + CHECKSUM_LEN; // 10

// 下载分块大小（规范 §3.4：512KB）
constexpr uint32_t DOWNLOAD_CHUNK_SIZE = 512 * 1024;
// 文件头帧发出后延迟推送首块的时间（客户端建进度条需要）
constexpr uint32_t DOWNLOAD_HEAD_DELAY_MS = 200;
// 远程监控推送帧率限制（规范：≤5fps → 每客户端最小间隔 200ms）
constexpr uint32_t REMOTEWATCH_INTERVAL_MS = 200;

// 监控推送线程数（SigLogicSystem 保留生产者-消费者仅用于远程监控组）
constexpr int REMOTEWATCH_THREAD_NUM = 1;

// ==================== 消息 ID（规范 §2.2，DataId 枚举）====================
enum class DataId : uint16_t
{
    NO_MESSAGE              = 0,
    LOGIN_USER              = 1000, // 远程登录
    GET_DRIVERINFO          = 1001, // 获取磁盘分区/根目录
    GET_DIRINFO             = 1002, // 获取目录内容（逐条推送）
    DOWNLOAD_FILE           = 1003, // 文件下载
    EXECUTE_FILE            = 1004, // 远程执行
    DELETE_FILE             = 1005, // 远程删除
    JOIN_TCP_REMOTEWATCH    = 1006, // 加入远程监控组
    GET_REMOTEWATCH_SCREEN  = 1007, // 推送屏幕截图（S→C）
    EXIT_TCP_REMOTEWATCH    = 1008, // 退出远程监控组
    MOUSE_REMOTE_CONTROL    = 1009, // 远程鼠标注入
    LOCK_REMOTE_CONTROL     = 1010, // 远程锁机
    UNLOCK_REMOTE_CONTROL   = 1011, // 远程解锁
    UPLOAD_FILE             = 1012, // 文件上传（客户端分块发送，服务端组包并回响应）
    CANCEL_UPLOAD_FILE      = 1013, // 取消上传（服务端丢弃后续该文件块并清理）
    CANCEL_DOWNLOAD         = 1014, // （预留）取消下载
    KEYBOARD_REMOTE_CONTROL = 1015, // 远程键盘注入（按下/释放）
    SEND_MSG                = 1100, // （预留）聊天消息
    SEND_FILE               = 1101, // （预留）聊天文件
};

// ==================== 远程监控状态（规范 §3.7，RemoteWatchStatus 枚举）====================
enum class RemoteWatchStatus : int
{
    WATCH_SUCCESS        = 0, // 监控启动成功
    WATCH_NO_DESKTOP     = 1, // 无桌面环境（Headless）
    WATCH_ENCODER_ERROR  = 2, // 实时编码器启动失败
    WATCH_SYSTEM_ERROR   = 3, // 系统错误
};

// ==================== 登录结果（规范 §3.1）====================
enum class LoginStatus : int
{
    LOGIN_SUCCESS          = 0,
    LOGIN_USERNAME_ERROR   = 1,
    LOGIN_PASSWORD_ERROR   = 2,
    LOGIN_SYSTEM_ERROR     = 3,
};

// ==================== Base64（规范：标准 base64 含 +/=）====================
namespace base64 {
    // 编码：二进制 → base64 字符串
    std::string encode(const char* data, size_t len);
    inline std::string encode(const std::string& s) { return encode(s.data(), s.size()); }
    // 解码：base64 字符串 → 二进制
    std::string decode(const char* data, size_t len);
    inline std::string decode(const std::string& s) { return decode(s.data(), s.size()); }
}

// JSON 解析/构建统一采用 jsoncpp 库（#include "jsoncpp/json/json.h"），不再使用 mini_json

#endif
