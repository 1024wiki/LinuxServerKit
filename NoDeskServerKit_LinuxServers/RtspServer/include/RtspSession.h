#ifndef RTSP_SESSION_H_
#define RTSP_SESSION_H_

#include "StreamTask.h"
#include "RtspRequest.h"
#include <unordered_map>
#include <mutex>

enum class RtspSessionState {
    INIT = 0,
    DESCRIBED,
    SETUP,
    PLAYING,
    PAUSED,
    TORN_DOWN
};

class RtspConnection;

class RtspSession {
private:
    std::string _session_id;
    std::string _file_path;
    std::string _client_ip;
    RtspSessionState _state = RtspSessionState::INIT;
    std::shared_ptr<StreamTask> _stream_task;
    std::weak_ptr<RtspConnection> _conn;

public:
    RtspSession(const std::string& session_id, const std::string& file_path,
                const std::string& client_ip, std::shared_ptr<RtspConnection> conn);
    ~RtspSession();

    const std::string& GetSessionId() const { return _session_id; }
    const std::string& GetFilePath() const { return _file_path; }
    RtspSessionState GetState() const { return _state; }
    void SetState(RtspSessionState s) { _state = s; }

    std::shared_ptr<StreamTask> GetStreamTask() { return _stream_task; }
    void SetStreamTask(std::shared_ptr<StreamTask> st) { _stream_task = st; }

    bool InitMedia();
    std::string GenerateSdp(const std::string& url);

    bool SetupTrack(int track_id, int client_rtp_port, int client_rtcp_port,
                    uint16_t& out_server_rtp_port, uint16_t& out_server_rtcp_port);

    void Play(double start_sec);
    void Pause();
    void Teardown();
};

class RtspSessionManager {
private:
    static std::shared_ptr<RtspSessionManager> instance;
    std::unordered_map<std::string, std::shared_ptr<RtspSession>> _sessions;
    std::mutex _mutex;

    RtspSessionManager() = default;

public:
    static std::shared_ptr<RtspSessionManager> GetInstance();

    std::shared_ptr<RtspSession> CreateSession(const std::string& file_path,
                                              const std::string& client_ip,
                                              std::shared_ptr<RtspConnection> conn);

    // 创建实时桌面会话 (/desktop)
    std::shared_ptr<RtspSession> CreateDesktopSession(const std::string& client_ip,
                                                     std::shared_ptr<RtspConnection> conn);

    std::shared_ptr<RtspSession> FindSession(const std::string& session_id);
    void RemoveSession(const std::string& session_id);
    void Clear();
};

#endif
