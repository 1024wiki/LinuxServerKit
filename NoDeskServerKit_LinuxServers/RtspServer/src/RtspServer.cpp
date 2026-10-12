#include "RtspServer.h"
#include "RtspConnection.h"

std::shared_ptr<RtspServer> RtspServer::instance = nullptr;

RtspServer::RtspServer(std::shared_ptr<EventLoop> eventloop, std::string ip, uint16_t port)
    : TcpServer(eventloop, ip, port) {}

RtspServer::RtspServer(int cpus, std::string ip, uint16_t port)
    : TcpServer(cpus, ip, port) {}

RtspServer::~RtspServer() {}

std::shared_ptr<RtspServer> RtspServer::GetInstances(int cpus, std::string ip, uint16_t port) {
    static std::once_flag once;
    std::call_once(once, [cpus, ip, port]() {
        RtspServer::instance.reset(new RtspServer(cpus, ip, port));
    });
    return RtspServer::instance;
}

std::shared_ptr<TcpConnection> RtspServer::CreateClientConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor) {
    return std::make_shared<RtspConnection>(sockfd, actor);
}
