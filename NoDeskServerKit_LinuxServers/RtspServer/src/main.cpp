#include "RtspServer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

int main(int argc, char** argv) {
    std::cout << "NoDeskServerKit RtspServer (VOD streaming server)" << std::endl;

    uint16_t port = DEFAULT_RTSP_PORT; // 8554
    if (argc > 1) {
        int p = std::atoi(argv[1]);
        if (p > 0 && p <= 65535) port = static_cast<uint16_t>(p);
    }

    std::shared_ptr<RtspServer> instance = RtspServer::GetInstances(0, "0.0.0.0", port);
    std::cout << "[RtspServer] listening on 0.0.0.0:" << port << std::endl;

    instance->Start(TcpServer::ReactorModel::MASTER_SLAVE);
    std::cout << "[RtspServer] running in background (waiting)..." << std::endl;

    static std::atomic<bool> keep_running{true};
    while (keep_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    instance->Close();
    return 0;
}
