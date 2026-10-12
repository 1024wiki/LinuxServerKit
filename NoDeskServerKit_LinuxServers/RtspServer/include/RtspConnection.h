#ifndef RTSP_CONNECTION_H_
#define RTSP_CONNECTION_H_

#include "TcpConnection.h"
#include "RtspRequest.h"
#include "RtspSession.h"

// 客户端 RTSP 控制连接器：继承自 ReactorTCPNet::TcpConnection
// 读回调解析 RTSP 文本请求并分发处理
class RtspConnection : public TcpConnection {
private:
    std::shared_ptr<RtspSession> _session;
    std::string _client_ip;
    bool _closed = false;
    std::mutex _close_mutex;

    void OnReadBussiness(ReadBuf& buf);
    void OnCloseBussiness();

    void HandleRequest(const RtspRequest& req);

    void HandleOptions(const RtspRequest& req);
    void HandleDescribe(const RtspRequest& req);
    void HandleSetup(const RtspRequest& req);
    void HandlePlay(const RtspRequest& req);
    void HandlePause(const RtspRequest& req);
    void HandleTeardown(const RtspRequest& req);

    void SendResponse(int status_code, const std::string& status_msg,
                      int cseq, const std::string& headers, const std::string& body = "");

public:
    RtspConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor);
    ~RtspConnection();

    void Close();
    void SetClientIp(const std::string& ip) { _client_ip = ip; }
    const std::string& GetClientIp() const { return _client_ip; }
};

#endif
