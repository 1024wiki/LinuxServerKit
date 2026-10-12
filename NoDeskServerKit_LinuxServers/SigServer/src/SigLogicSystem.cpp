#include "SigLogicSystem.h"
#include "jsoncpp/json/json.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <climits>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

// X11 鼠标注入（规范 §3.8）
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

// 静态单例
std::shared_ptr<SigLogicSystem> SigLogicSystem::instance = nullptr;

// ==================== 构造/析构 ====================
SigLogicSystem::SigLogicSystem()
{
    // 初始化消息ID→处理函数表
    init_id_function();

    // 启动远程监控推送线程（不变）
    for (int i = 0; i < REMOTEWATCH_THREAD_NUM; ++i)
        send_remotewatch_threads.emplace_back(&SigLogicSystem::send_remotewatch_thread_work, this);

    // X11 鼠标注入 display
    display_mutex.lock();
    display = XOpenDisplay(NULL);
    if (!display)
        std::cerr << "[SigLogicSystem] XOpenDisplay failed, mouse control disabled" << std::endl;
    display_mutex.unlock();
}

SigLogicSystem::~SigLogicSystem()
{
    Stop();
}

void SigLogicSystem::Stop()
{
    if (!server_running.load()) return;
    server_running.store(false);

    // 唤醒并回收监控推送线程
    watch_cond_consume.notify_all();
    for (std::thread& t : send_remotewatch_threads)
        if (t.joinable()) t.join();

    // 关闭 X11 display
    display_mutex.lock();
    if (display) { XCloseDisplay(display); display = nullptr; }
    display_mutex.unlock();
}

// ==================== 单例 ====================
std::shared_ptr<SigLogicSystem> SigLogicSystem::GetInstances()
{
    static std::once_flag once;
    std::call_once(once, [](){
        instance.reset(new SigLogicSystem());
    });
    return instance;
}

// ==================== 直接处理（Reactor 事件循环内同步执行，无生产者-消费者）====================
void SigLogicSystem::HandlePacket(std::shared_ptr<SigConnection> conn, std::shared_ptr<TcpPacket> packet)
{
    if (!conn || !packet) return;

    DataId id = packet->get_messageid();
    auto it = id_func_map.find(id);
    if (it != id_func_map.end()) {
        try {
            it->second(packet->get_messagedata(), conn);
        } catch (const std::exception& e) {
            std::cerr << "[SigLogicSystem] handler exception id=" << (int)id
                      << " : " << e.what() << std::endl;
        }
    } else {
        std::cerr << "[SigLogicSystem] unknown DataId: " << (unsigned)id << std::endl;
    }
}

// ==================== 远程监控组管理 ====================
void SigLogicSystem::add_remotewatch_client(std::shared_ptr<SigConnection> conn)
{
    bool inserted;
    {
        std::lock_guard<std::mutex> lock(watch_set_mutex);
        inserted = remotewatch_set.insert(conn->Uid()).second;
    }
    if (inserted) {
        {
            std::lock_guard<std::mutex> lock(watch_queue_mutex);
            remotewatch_queue.push(conn);
        }
        conn->SetInRemoteWatch(true);
        watch_cond_consume.notify_one();
    }
}

void SigLogicSystem::delete_remotewatch_client(std::shared_ptr<SigConnection> conn)
{
    {
        std::lock_guard<std::mutex> lock(watch_set_mutex);
        remotewatch_set.erase(conn->Uid());
    }
    conn->SetInRemoteWatch(false);
}

// 连接关闭时：移出监控组 + 重置下载
void SigLogicSystem::remove_client(std::shared_ptr<SigConnection> conn)
{
    if (conn->InRemoteWatch())
        delete_remotewatch_client(conn);
    conn->ResetDownload();
}

// ==================== 监控推送线程 ====================
void SigLogicSystem::send_remotewatch_thread_work()
{
    while (server_running.load()) {
        std::shared_ptr<SigConnection> conn;
        {
            std::unique_lock<std::mutex> lock(watch_queue_mutex);
            watch_cond_consume.wait(lock, [this](){
                return !server_running.load() || !remotewatch_queue.empty();
            });
            if (!server_running.load()) break;
            conn = remotewatch_queue.front();
            remotewatch_queue.pop();
        }

        if (!conn || conn->GetState() == ConnState::CLOSED) {
            // 连接已关闭，跳过
            if (conn) delete_remotewatch_client(conn);
            continue;
        }

        // 截屏（规范 §3.7 建议：缩放到小尺寸/JPEG，以控制带宽防阻塞 TCP 流）
        // 这里降采样（如固定到 320x240 RGB 缩略图）生成体积约 150KB 的截图，避免单帧 9MB 冲垮发送缓冲区
        std::string b64;
        bool ok = false;
        display_mutex.lock();
        if (display) {
            int scr = DefaultScreen(display);
            Window root = RootWindow(display, scr);
            int w = DisplayWidth(display, scr);
            int h = DisplayHeight(display, scr);
            XImage* img = XGetImage(display, root, 0, 0, w, h, AllPlanes, ZPixmap);
            if (img) {
                // 等比缩放采样到最大宽 320、高 240
                int target_w = (w > 320) ? 320 : w;
                int target_h = (h > 240) ? 240 : h;
                size_t raw_len = (size_t)target_w * target_h * 3;
                std::string raw(raw_len, '\0');
                for (int ty = 0; ty < target_h; ++ty) {
                    int src_y = (ty * h) / target_h;
                    for (int tx = 0; tx < target_w; ++tx) {
                        int src_x = (tx * w) / target_w;
                        unsigned long px = XGetPixel(img, src_x, src_y);
                        size_t off = ((size_t)ty * target_w + tx) * 3;
                        raw[off + 0] = (px >> 16) & 0xFF; // R
                        raw[off + 1] = (px >>  8) & 0xFF; // G
                        raw[off + 2] =  px        & 0xFF; // B
                    }
                }
                XDestroyImage(img);
                b64 = base64::encode(raw.data(), raw.size());
                ok = true;
            }
        }
        display_mutex.unlock();

        // 构造 1007 GET_REMOTEWATCH_SCREEN 响应（jsoncpp）
        Json::Value resp;
        resp["error"] = ok ? 0 : 1;
        resp["screenpix"] = b64;
        conn->SendJson(DataId::GET_REMOTEWATCH_SCREEN, Json::writeString(Json::StreamWriterBuilder(), resp));

        // 若仍在监控组，重新入队尾（轮转），并控制帧率
        {
            std::lock_guard<std::mutex> lock(watch_set_mutex);
            if (remotewatch_set.count(conn->Uid())) {
                {
                    std::lock_guard<std::mutex> qlock(watch_queue_mutex);
                    remotewatch_queue.push(conn);
                }
                watch_cond_consume.notify_one();
            }
        }
        // 帧率节流（规范：≤5fps）
        std::this_thread::sleep_for(std::chrono::milliseconds(REMOTEWATCH_INTERVAL_MS));
    }
}
