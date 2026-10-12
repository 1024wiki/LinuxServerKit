#include "SigConnection.h"
#include "SigLogicSystem.h"
#include <iostream>
#include <random>
#include <climits>
#include <cstdlib>
#include <arpa/inet.h>

// ==================== 工具：生成连接唯一标识 ====================
static uint64_t GenerateUid()
{
    static std::atomic<uint64_t> counter{0};
    uint64_t t = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    return (t << 20) | (counter.fetch_add(1) & 0xFFFFF);
}

SigConnection::SigConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor)
    : TcpConnection(sockfd, actor)
{
    _uid = GenerateUid();

    // 注册读事件业务回调（规范 §2.4 状态机）
    SetReadTcpCBDealCB(std::bind(&SigConnection::OnReadBussiness, this, std::placeholders::_1));
    // 注册关闭事件业务回调
    SetCloseTcpCBDealCB(std::bind(&SigConnection::OnCloseBussiness, this));
}

SigConnection::~SigConnection()
{
    Close();
}

// ==================== 读事件：帧解析状态机 ====================
// 解析出完整帧 → 构造 TcpPacket → 直接在 Reactor 事件循环中同步处理（不入队）
void SigConnection::OnReadBussiness(ReadBuf& buf)
{
    std::cerr << "[SigConn fd=" << GetSockFD() << "] OnReadBussiness avail=" << buf.ReadableDataSize() << std::endl;
    // 循环解析缓冲区内的所有完整帧（单次读取可能包含多帧）
    while (true) {
        DataId id = DataId::NO_MESSAGE;
        std::shared_ptr<char[]> body;
        uint32_t body_len = 0;

        TcpPacket::ParseResult r = TcpPacket::Parse(buf, id, body, body_len);
        if (r == TcpPacket::ParseResult::NEED_MORE)
            break;  // 数据不足，等待后续字节
        if (r == TcpPacket::ParseResult::BAD_FRAME)
            continue; // 校验失败已丢弃，继续重扫

        // PARSED：构造 TcpPacket 并在当前 Reactor 事件循环中直接同步处理（不使用生产者-消费者）
        auto pkt = std::make_shared<TcpPacket>(id, body, body_len);
        // TcpConnection 继承 enable_shared_from_this，转换为本类 shared_ptr
        std::shared_ptr<SigConnection> self = std::static_pointer_cast<SigConnection>(shared_from_this());
        SigLogicSystem::GetInstances()->HandlePacket(self, pkt);
    }
}

// ==================== 关闭事件：业务清理 ====================
void SigConnection::OnCloseBussiness()
{
    Close();
}

void SigConnection::Close()
{
    std::cerr << "[SigConn fd=" << GetSockFD() << "] Close() called" << std::endl;
    {
        std::lock_guard<std::mutex> lock(_close_mutex);
        if (_closed) return;
        _closed = true;
    }
    _state.store(ConnState::CLOSED);

    // 通知逻辑系统清理（移出监控组、重置下载任务）
    std::shared_ptr<SigConnection> self = std::static_pointer_cast<SigConnection>(shared_from_this());
    SigLogicSystem::GetInstances()->remove_client(self);

    // 重置下载任务与未完成的上传任务
    ResetDownload();
    ClearAllUploadTasks(true);

    // 断开 TCP 并释放 Channel 资源（TcpConnection::CloseTcpConnection 幂等）
    CloseTcpConnection();
}

// ==================== 上传任务管理 ====================
std::shared_ptr<UploadTask> SigConnection::GetUploadTask(const std::string& remotepath)
{
    std::lock_guard<std::mutex> lock(_upload_mutex);
    auto it = _upload_tasks.find(remotepath);
    return (it != _upload_tasks.end()) ? it->second : nullptr;
}

std::shared_ptr<UploadTask> SigConnection::CreateUploadTask(const std::string& remotepath,
                                                            const std::string& localpath,
                                                            int64_t totallen)
{
    std::lock_guard<std::mutex> lock(_upload_mutex);
    auto task = std::make_shared<UploadTask>();
    task->remotepath = remotepath;
    task->localpath = localpath;
    task->totallen = totallen;
    task->recvlen = 0;
    task->cancelled = false;

    // 二进制写入模式打开/覆盖目标文件
    task->ofs.open(remotepath, std::ios::binary | std::ios::out | std::ios::trunc);
    task->is_open = task->ofs.is_open();

    _upload_tasks[remotepath] = task;
    return task;
}

void SigConnection::RemoveUploadTask(const std::string& remotepath, bool delete_file)
{
    std::shared_ptr<UploadTask> task;
    {
        std::lock_guard<std::mutex> lock(_upload_mutex);
        auto it = _upload_tasks.find(remotepath);
        if (it != _upload_tasks.end()) {
            task = it->second;
            _upload_tasks.erase(it);
        }
    }
    if (task) {
        if (task->ofs.is_open()) {
            task->ofs.close();
        }
        if (delete_file) {
            unlink(remotepath.c_str());
        }
    }
}

void SigConnection::ClearAllUploadTasks(bool delete_files)
{
    std::vector<std::shared_ptr<UploadTask>> tasks;
    {
        std::lock_guard<std::mutex> lock(_upload_mutex);
        for (auto& pair : _upload_tasks) {
            tasks.push_back(pair.second);
        }
        _upload_tasks.clear();
    }
    for (auto& task : tasks) {
        if (task && task->ofs.is_open()) {
            task->ofs.close();
            if (delete_files) {
                unlink(task->remotepath.c_str());
            }
        }
    }
}

// ==================== 帧发送 ====================
void SigConnection::SendFrame(DataId id, const char* body, uint32_t body_len)
{
    if (_state.load() == ConnState::CLOSED) return;
    uint32_t total = 0;
    std::shared_ptr<char[]> frame = TcpPacket::Build(id, body, body_len, total);
    if (frame)
        Send(total, frame);   // 入 WriteBuf 队列，EPOLLOUT 异步发送
}

void SigConnection::SendJson(DataId id, const std::string& json)
{
    SendFrame(id, json.data(), (uint32_t)json.size());
}

void SigConnection::SendRaw(DataId id, const char* body, uint32_t body_len)
{
    SendFrame(id, body, body_len);
}

// ==================== 路径安全校验（规范 §3.6/§4.1）====================
bool SigConnection::CheckPathInHome(const std::string& path) const
{
    return !SafeRealPath(path).empty();
}

std::string SigConnection::SafeRealPath(const std::string& path) const
{
    if (_home_dir.empty()) return "";   // 未登录
    if (path.empty()) return "";

    // realpath 解析符号链接与 ../ 后比较前缀
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == nullptr)
        return "";

    std::string real(resolved);
    // 必须位于家目录内（含家目录本身）
    if (real == _home_dir) return real;
    if (real.compare(0, _home_dir.size() + 1, _home_dir + "/") == 0)
        return real;
    return "";
}
