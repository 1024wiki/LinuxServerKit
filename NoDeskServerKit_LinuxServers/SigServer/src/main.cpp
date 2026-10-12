#include <iostream>
#include "SigServer.h"
#include "SigLogicSystem.h"
#include <map>

int main(int argc, char** argv){
    std::cout << "NoDeskServerKit SigServer (signaling server)" << std::endl;

    // 监听端口：命令行参数可覆盖（默认 10086，规范默认端口）
    uint16_t port = 10086;
    if (argc > 1) {
        int p = atoi(argv[1]);
        if (p > 0 && p <= 65535) port = (uint16_t)p;
    }

    // 创建单例信令服务器，监听 0.0.0.0:<port>
    std::shared_ptr<SigServer> instance = SigServer::GetInstances(0, "0.0.0.0", port);
    std::cout << "[SigServer] listening on 0.0.0.0:" << port << std::endl;

    // 启动事件循环（主从 Reactor，反应器数量为 CPU 核心数）
    // 注意：EventLoop::Loop() 启动各反应器子线程后立即返回。
    // 在子线程运行期间，主线程阻塞等待终止信号，以支持后台（& / daemon）部署。
    instance->Start(TcpServer::ReactorModel::MASTER_SLAVE);

    std::cout << "[SigServer] running in background (waiting)..." << std::endl;
    // 捕获终止标志或休眠等待，避免 std::cin.get() 在无 tty / 重定向输入时立即 EOF 退出
    static std::atomic<bool> keep_running{true};
    while (keep_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 事件循环退出后清理逻辑系统
    SigLogicSystem::GetInstances()->Stop();

    instance->Close();
    return 0;
}

