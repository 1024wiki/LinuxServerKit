#ifndef RTSP_SERVER_H_
#define RTSP_SERVER_H_

#include "TcpServer.h"
#include "RtspCommon.h"

// RTSP 流媒体服务器：继承自 ReactorTCPNet::TcpServer
class RtspServer : public TcpServer {
private:
    static std::shared_ptr<RtspServer> instance;

    RtspServer(std::shared_ptr<EventLoop> eventloop, std::string ip = "0.0.0.0", uint16_t port = DEFAULT_RTSP_PORT);
    RtspServer(int cpus = 0, std::string ip = "0.0.0.0", uint16_t port = DEFAULT_RTSP_PORT);

public:
    ~RtspServer();

    static std::shared_ptr<RtspServer> GetInstances(int cpus = 0, std::string ip = "0.0.0.0", uint16_t port = DEFAULT_RTSP_PORT);

    // 重写连接工厂方法以创建派生的 RtspConnection
    std::shared_ptr<TcpConnection> CreateClientConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor) override;
};

#endif
