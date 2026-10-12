#include "RtspConnection.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <climits>

RtspConnection::RtspConnection(int sockfd, std::shared_ptr<EpollTaskScheduler> actor)
    : TcpConnection(sockfd, actor)
{
    // 获取对端 IP 地址
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    if (getpeername(sockfd, (sockaddr*)&peer, &len) == 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip_str, sizeof(ip_str));
        _client_ip = ip_str;
    } else {
        _client_ip = "127.0.0.1";
    }

    SetReadTcpCBDealCB(std::bind(&RtspConnection::OnReadBussiness, this, std::placeholders::_1));
    SetCloseTcpCBDealCB(std::bind(&RtspConnection::OnCloseBussiness, this));
}

RtspConnection::~RtspConnection() {
    Close();
}

void RtspConnection::OnCloseBussiness() {
    Close();
}

void RtspConnection::Close() {
    {
        std::lock_guard<std::mutex> lock(_close_mutex);
        if (_closed) return;
        _closed = true;
    }

    if (_session) {
        RtspSessionManager::GetInstance()->RemoveSession(_session->GetSessionId());
        _session->Teardown();
        _session.reset();
    }

    CloseTcpConnection();
}

void RtspConnection::OnReadBussiness(ReadBuf& buf) {
    while (buf.ReadableDataSize() > 0) {
        RtspRequest req;
        size_t parsed_bytes = 0;
        bool ok = RtspParser::Parse(buf.ReadStartPos(), buf.ReadableDataSize(), req, parsed_bytes);
        if (!ok) {
            // 半包，等待更多数据
            break;
        }

        buf.OffsetStartPos(parsed_bytes);
        HandleRequest(req);
    }
}

void RtspConnection::SendResponse(int status_code, const std::string& status_msg,
                                  int cseq, const std::string& headers, const std::string& body)
{
    std::ostringstream ss;
    ss << "RTSP/1.0 " << status_code << " " << status_msg << "\r\n";
    ss << "CSeq: " << cseq << "\r\n";
    if (!headers.empty()) {
        ss << headers;
        if (headers.size() >= 2 && (headers.substr(headers.size() - 2) != "\r\n")) {
            ss << "\r\n";
        }
    }
    if (!body.empty()) {
        ss << "Content-Length: " << body.size() << "\r\n";
    }
    ss << "\r\n";
    if (!body.empty()) {
        ss << body;
    }

    std::string response = ss.str();
    Send((uint32_t)response.size(), response.c_str());
}

void RtspConnection::HandleRequest(const RtspRequest& req) {
    switch (req.method) {
        case RtspMethod::OPTIONS:  HandleOptions(req); break;
        case RtspMethod::DESCRIBE: HandleDescribe(req); break;
        case RtspMethod::SETUP:    HandleSetup(req); break;
        case RtspMethod::PLAY:     HandlePlay(req); break;
        case RtspMethod::PAUSE:    HandlePause(req); break;
        case RtspMethod::TEARDOWN: HandleTeardown(req); break;
        default:
            SendResponse(501, "Not Implemented", req.cseq, "");
            break;
    }
}

void RtspConnection::HandleOptions(const RtspRequest& req) {
    std::string hdrs = "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN\r\n";
    SendResponse(200, "OK", req.cseq, hdrs);
}

void RtspConnection::HandleDescribe(const RtspRequest& req) {
    std::string decoded_url = rtsp_url::decode(req.url);

    // 检查是否为方案 B 实时桌面推流: rtsp://ip:8554/desktop
    bool is_desktop = (decoded_url.find("/desktop") != std::string::npos);

    if (is_desktop) {
        _session = RtspSessionManager::GetInstance()->CreateDesktopSession(_client_ip,
            std::static_pointer_cast<RtspConnection>(shared_from_this()));

        if (!_session || !_session->InitMedia()) {
            RTSP_ERR("Describe: InitDesktop failed for %s", req.url.c_str());
            SendResponse(404, "Not Found (No Desktop)", req.cseq, "");
            return;
        }

        std::string sdp = _session->GenerateSdp(req.url);
        _session->SetState(RtspSessionState::DESCRIBED);

        std::ostringstream hdrs;
        hdrs << "Content-Type: application/sdp\r\n";
        hdrs << "Content-Base: " << req.url << "/\r\n";
        SendResponse(200, "OK", req.cseq, hdrs.str(), sdp);
        return;
    }

    // 否则为标准视频点播: rtsp://ip:8554/stream/<file_path>
    std::string file_path = rtsp_url::extract_file_path(req.url);
    if (file_path.empty()) {
        RTSP_ERR("Describe: invalid url %s", req.url.c_str());
        SendResponse(400, "Bad Request", req.cseq, "");
        return;
    }

    // 路径防 ../ 穿越安全校验
    char resolved[PATH_MAX];
    if (realpath(file_path.c_str(), resolved) == nullptr) {
        RTSP_ERR("Describe: file not found %s", file_path.c_str());
        SendResponse(404, "Not Found", req.cseq, "");
        return;
    }
    std::string safe_path(resolved);

    // 创建或复用会话
    _session = RtspSessionManager::GetInstance()->CreateSession(safe_path, _client_ip,
        std::static_pointer_cast<RtspConnection>(shared_from_this()));

    if (!_session || !_session->InitMedia()) {
        RTSP_ERR("Describe: InitMedia failed for %s", safe_path.c_str());
        SendResponse(404, "Not Found", req.cseq, "");
        return;
    }

    std::string sdp = _session->GenerateSdp(req.url);
    _session->SetState(RtspSessionState::DESCRIBED);

    std::ostringstream hdrs;
    hdrs << "Content-Type: application/sdp\r\n";
    hdrs << "Content-Base: " << req.url << "/\r\n";
    SendResponse(200, "OK", req.cseq, hdrs.str(), sdp);
}

void RtspConnection::HandleSetup(const RtspRequest& req) {
    std::shared_ptr<RtspSession> sess = _session;
    if (!sess && !req.session_id.empty()) {
        sess = RtspSessionManager::GetInstance()->FindSession(req.session_id);
    }

    if (!sess) {
        SendResponse(454, "Session Not Found", req.cseq, "");
        return;
    }

    int track_id = req.track_id;
    if (track_id < 0 || track_id > 1) {
        // 如果 url 里没有 trackID，尝试看已有配置
        track_id = 0;
    }

    uint16_t s_rtp = 0, s_rtcp = 0;
    bool ok = sess->SetupTrack(track_id, req.client_rtp_port, req.client_rtcp_port, s_rtp, s_rtcp);
    if (!ok) {
        SendResponse(500, "Internal Server Error", req.cseq, "");
        return;
    }

    std::ostringstream hdrs;
    hdrs << "Transport: RTP/AVP;unicast;client_port=" << req.client_rtp_port << "-" << req.client_rtcp_port
         << ";server_port=" << s_rtp << "-" << s_rtcp << "\r\n";
    hdrs << "Session: " << sess->GetSessionId() << ";timeout=60\r\n";

    SendResponse(200, "OK", req.cseq, hdrs.str());
}

void RtspConnection::HandlePlay(const RtspRequest& req) {
    std::shared_ptr<RtspSession> sess = _session;
    if (!sess && !req.session_id.empty()) {
        sess = RtspSessionManager::GetInstance()->FindSession(req.session_id);
    }
    if (!sess) {
        SendResponse(454, "Session Not Found", req.cseq, "");
        return;
    }

    double start_sec = req.has_range ? req.range_start : 0.0;
    sess->Play(start_sec);

    std::ostringstream hdrs;
    hdrs << "Session: " << sess->GetSessionId() << "\r\n";
    hdrs << "Range: npt=" << std::fixed << std::setprecision(3) << start_sec << "-\r\n";

    SendResponse(200, "OK", req.cseq, hdrs.str());
}

void RtspConnection::HandlePause(const RtspRequest& req) {
    std::shared_ptr<RtspSession> sess = _session;
    if (!sess && !req.session_id.empty()) {
        sess = RtspSessionManager::GetInstance()->FindSession(req.session_id);
    }
    if (!sess) {
        SendResponse(454, "Session Not Found", req.cseq, "");
        return;
    }

    sess->Pause();

    std::ostringstream hdrs;
    hdrs << "Session: " << sess->GetSessionId() << "\r\n";
    SendResponse(200, "OK", req.cseq, hdrs.str());
}

void RtspConnection::HandleTeardown(const RtspRequest& req) {
    std::shared_ptr<RtspSession> sess = _session;
    if (!sess && !req.session_id.empty()) {
        sess = RtspSessionManager::GetInstance()->FindSession(req.session_id);
    }

    if (sess) {
        sess->Teardown();
        RtspSessionManager::GetInstance()->RemoveSession(sess->GetSessionId());
        _session.reset();
    }

    std::string hdrs;
    if (sess) {
        hdrs = "Session: " + sess->GetSessionId() + "\r\n";
    }
    SendResponse(200, "OK", req.cseq, hdrs);
}
