#include "RtspSession.h"
#include "DesktopStreamTask.h"
#include <random>
#include <sstream>

std::shared_ptr<RtspSessionManager> RtspSessionManager::instance = nullptr;

std::shared_ptr<RtspSessionManager> RtspSessionManager::GetInstance() {
    static std::once_flag once;
    std::call_once(once, []() {
        instance.reset(new RtspSessionManager());
    });
    return instance;
}

std::shared_ptr<RtspSession> RtspSessionManager::CreateSession(const std::string& file_path,
                                                              const std::string& client_ip,
                                                              std::shared_ptr<RtspConnection> conn)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 生成 16 位 16 进制会话 ID
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
    std::string sess_id = ss.str();

    auto sess = std::make_shared<RtspSession>(sess_id, file_path, client_ip, conn);
    _sessions[sess_id] = sess;
    return sess;
}

std::shared_ptr<RtspSession> RtspSessionManager::CreateDesktopSession(const std::string& client_ip,
                                                                     std::shared_ptr<RtspConnection> conn)
{
    std::lock_guard<std::mutex> lock(_mutex);

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
    std::string sess_id = ss.str();

    auto sess = std::make_shared<RtspSession>(sess_id, "desktop_live", client_ip, conn);
    // 替换为 DesktopStreamTask
    auto dt = std::make_shared<DesktopStreamTask>(client_ip);
    sess->SetStreamTask(dt);
    _sessions[sess_id] = sess;
    return sess;
}

std::shared_ptr<RtspSession> RtspSessionManager::FindSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _sessions.find(session_id);
    return (it != _sessions.end()) ? it->second : nullptr;
}

void RtspSessionManager::RemoveSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(_mutex);
    _sessions.erase(session_id);
}

void RtspSessionManager::Clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    _sessions.clear();
}

// ----------------- RtspSession -----------------
RtspSession::RtspSession(const std::string& session_id, const std::string& file_path,
                         const std::string& client_ip, std::shared_ptr<RtspConnection> conn)
    : _session_id(session_id), _file_path(file_path), _client_ip(client_ip), _conn(conn)
{
    _stream_task = std::make_shared<StreamTask>(file_path, client_ip);
}

RtspSession::~RtspSession() {
    Teardown();
}

bool RtspSession::InitMedia() {
    if (!_stream_task) return false;
    auto dt = std::dynamic_pointer_cast<DesktopStreamTask>(_stream_task);
    if (dt) {
        return dt->InitDesktop();
    }
    return _stream_task->InitMedia();
}

std::string RtspSession::GenerateSdp(const std::string& url) {
    return _stream_task ? _stream_task->GenerateSdp(url) : "";
}

bool RtspSession::SetupTrack(int track_id, int client_rtp_port, int client_rtcp_port,
                            uint16_t& out_server_rtp_port, uint16_t& out_server_rtcp_port)
{
    if (!_stream_task) return false;
    bool ok = _stream_task->SetupTrack(track_id, client_rtp_port, client_rtcp_port,
                                       out_server_rtp_port, out_server_rtcp_port);
    if (ok) {
        _state = RtspSessionState::SETUP;
    }
    return ok;
}

void RtspSession::Play(double start_sec) {
    if (_stream_task) {
        _stream_task->Play(start_sec);
        _state = RtspSessionState::PLAYING;
    }
}

void RtspSession::Pause() {
    if (_stream_task) {
        _stream_task->Pause();
        _state = RtspSessionState::PAUSED;
    }
}

void RtspSession::Teardown() {
    if (_stream_task) {
        _stream_task->Stop();
    }
    _state = RtspSessionState::TORN_DOWN;
}
