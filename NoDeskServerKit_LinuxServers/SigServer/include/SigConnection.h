#ifndef SIGCONNECTION_H_
#define SIGCONNECTION_H_

#include "define.h"
#include "TcpPacket.h"
#include "TcpConnection.h"
#include <fstream>

// 连接状态：未登录 → 已登录（绑定系统用户名）→ 关闭
enum class ConnState : uint8_t {
    UNAUTH = 0,
    AUTHED,
    CLOSED,
};

// 下载任务状态：分块推送大文件时维护（规范 §3.4）
struct DownloadTask {
    std::ifstream ifs;
    uint64_t total = 0;            // 文件总字节数
    uint64_t sent  = 0;            // 已读出（入队）的字节数
    uint32_t index = 0;            // 块序号，首块文件头为 0，分块从 1 起
    std::string sourcesavepath;    // 客户端本地保存目录（原样回传）
    std::string destfilepath;      // 服务端被下载文件绝对路径
    bool head_sent = false;        // 文件头帧是否已发出
    bool cancelled = false;        // 取消标志（预留 CANCEL_DOWNLOAD）
    std::string buf;               // 当前读块缓冲
};

// 上传任务状态：接收客户端 512KB 分块时维护（规范 §3.4A/§3.4B）
struct UploadTask {
    std::ofstream ofs;
    std::string remotepath;        // 服务端目标文件路径
    std::string localpath;         // 客户端本地路径
    int64_t totallen = 0;          // 文件总字节数
    int64_t recvlen = 0;           // 累计写入字节数
    bool cancelled = false;        // 是否已被取消上传
    bool is_open = false;
};

// 客户端信令连接器：每客户端一个，绑定到某个 reactor 的 Channel 上
// 读事件 → 解析二进制帧 → 投递 SigLogicSystem 逻辑队列（生产者-消费者）
// 注：TcpConnection 已继承 std::enable_shared_from_this，直接复用其 shared_from_this
//     （不可再继承一次，否则产生多继承二义）
class SigConnection : public TcpConnection
{
public:
    SigConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor);
    ~SigConnection();

    // —— 回调（构造时通过 SetReadTcpCBDealCB / SetCloseTcpCBDealCB 注册）——
    void OnReadBussiness(ReadBuf& buf);
    void OnCloseBussiness();

    // —— 供 SigLogicSystem 使用 ——
    // 发送一个信令帧（id + JSON body），内部 TcpPacket::Build 后 Send()
    void SendJson(DataId id, const std::string& json);
    // 发送一个信令帧（raw body，非 JSON 场景）
    void SendRaw(DataId id, const char* body, uint32_t body_len);

    // 登录状态与身份
    ConnState GetState() const { return _state.load(); }
    void SetState(ConnState s) { _state.store(s); }
    const std::string& Username() const { return _username; }
    void SetUsername(const std::string& u) { _username = u; }
    const std::string& HomeDir() const { return _home_dir; }
    void SetHomeDir(const std::string& d) { _home_dir = d; }

    // 远程监控组标志
    bool InRemoteWatch() const { return _in_watch.load(); }
    void SetInRemoteWatch(bool v) { _in_watch.store(v); }

    // —— 路径安全校验（规范 §3.6/§4.1）：路径必须在登录用户家目录内，防 ../ 穿越 ——
    bool CheckPathInHome(const std::string& path) const;
    // 解析客户端请求的绝对路径并做白名单校验，返回 realpath 后的路径；失败返回空
    std::string SafeRealPath(const std::string& path) const;

    // —— 下载任务 ——
    bool HasDownload() const { return _dl_task != nullptr; }
    void ResetDownload() { _dl_task.reset(); }

    // —— 上传任务 ——
    std::shared_ptr<UploadTask> GetUploadTask(const std::string& remotepath);
    std::shared_ptr<UploadTask> CreateUploadTask(const std::string& remotepath, const std::string& localpath, int64_t totallen);
    void RemoveUploadTask(const std::string& remotepath, bool delete_file = false);
    void ClearAllUploadTasks(bool delete_files = false);

    // —— 关闭连接（覆盖父类，做业务清理后再断 TCP）——
    void Close();

    // 获取一个稳定、线程安全的连接标识（用于监控组 set 等）
    uint64_t Uid() const { return _uid; }

private:
    // 初始化登录状态
    std::atomic<ConnState> _state{ConnState::UNAUTH};
    std::string _username;   // 登录成功后绑定的系统用户名
    std::string _home_dir;   // /home/<username>，路径白名单根
    std::atomic<bool> _in_watch{false};  // 是否在远程监控组
    uint64_t _uid;           // 连接唯一标识（构造时生成）

    // 下载任务（仅一个并发下载/连接；由逻辑线程持有，读写时需注意与读事件线程的同步）
    std::unique_ptr<DownloadTask> _dl_task;
    std::mutex _dl_mutex;    // 保护 _dl_task

    // 上传任务集合（支持同一连接多个文件上传任务，以 remotepath 为 key）
    std::unordered_map<std::string, std::shared_ptr<UploadTask>> _upload_tasks;
    std::mutex _upload_mutex;

    bool _closed = false;    // Close() 幂等标志
    std::mutex _close_mutex;

    // 发送原始字节（内部统一入口）
    void SendFrame(DataId id, const char* body, uint32_t body_len);

    // 下载流控：推进一块发送（文件头 / 512KB 块），由逻辑线程调用
    void PumpDownload();

    // 供 LogicSystem（作为友元或通过公开接口）操作下载任务
    friend class SigLogicSystem;
};

#endif
