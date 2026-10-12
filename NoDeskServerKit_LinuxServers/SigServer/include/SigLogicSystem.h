#ifndef SIGLOGICSYSTEM_H_
#define SIGLOGICSYSTEM_H_

#include "define.h"
#include "TcpPacket.h"
#include "SigConnection.h"

// 前置声明
struct _XDisplay;
typedef struct _XDisplay Display;

// ============================================================
// SigLogicSystem：信令包处理系统（不采用生产者-消费者，直接在 Reactor 事件循环中处理）
//   - 处理时机：在 SigConnection::OnReadBussiness 中解析出完整帧后，
//             直接调用 HandlePacket() 在当前 reactor 线程同步处理
//   - 回调表 id_func_map：DataId → handler(shared_ptr<char[]> body, shared_ptr<SigConnection>)
//   - 远程监控组：remotewatch_queue + remotewatch_set，独立线程周期截图推送（不变）
//   - 鼠标注入：X11 XTest（display_mutex 保护）
//   - JSON：采用 jsoncpp 库（#include "jsoncpp/json/json.h"）
// 单例（std::call_once 线程安全）
// ============================================================
class SigLogicSystem
{
public:
    // 消息处理器签名（与 LogicSystem 中一致）
    using PacketHandler = std::function<void(std::shared_ptr<char[]>,
                                              std::shared_ptr<SigConnection>)>;

private:
    // 服务器运行标志
    std::atomic<bool> server_running{true};

    // 消息ID → 处理函数映射表（核心：LogicSystem 的 id_func_map 模式）
    std::map<DataId, PacketHandler> id_func_map;

    // 远程监控组（不变）
    std::queue<std::shared_ptr<SigConnection>> remotewatch_queue;
    std::unordered_set<uint64_t> remotewatch_set;   // uid 集合（去重）
    std::mutex watch_queue_mutex;
    std::mutex watch_set_mutex;
    std::condition_variable watch_cond_consume;
    std::vector<std::thread> send_remotewatch_threads;

    // X11 Display（鼠标注入 + 截图）
    Display* display = nullptr;
    std::mutex display_mutex;

    // 单例
    static std::shared_ptr<SigLogicSystem> instance;

    SigLogicSystem();
    void init_id_function();                  // 注册全部 DataId 处理器
    void send_remotewatch_thread_work();      // 监控推送线程

    void add_remotewatch_client(std::shared_ptr<SigConnection>);
    void delete_remotewatch_client(std::shared_ptr<SigConnection>);

public:
    ~SigLogicSystem();
    SigLogicSystem(const SigLogicSystem&) = delete;
    SigLogicSystem& operator=(const SigLogicSystem&) = delete;
    SigLogicSystem(SigLogicSystem&&) = delete;
    SigLogicSystem& operator=(SigLogicSystem&&) = delete;

    // 获取单例
    static std::shared_ptr<SigLogicSystem> GetInstances();

    // 直接处理解析完成的包（由 epoll 线程在 SigConnection::OnReadBussiness 中同步调用）
    void HandlePacket(std::shared_ptr<SigConnection> conn, std::shared_ptr<TcpPacket> packet);

    // 连接关闭时：移出监控组、清理下载任务（由 SigConnection::Close 调用）
    void remove_client(std::shared_ptr<SigConnection> conn);

    // 停止逻辑系统（服务器关闭时调用）
    void Stop();
};

#endif
