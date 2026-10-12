// RtspClientTest: 针对 RtspServer 的全交互流程测试客户端
// 测试握手及控制方法:
//   1. OPTIONS
//   2. DESCRIBE (解析 SDP 中的 trackID 与 sprop-parameter-sets)
//   3. SETUP (建立 UDP 通道，绑定客户端接收端口)
//   4. PLAY (开启推流，在客户端接收并校验 RTP 视频包)
//   5. PAUSE
//   6. PLAY (Range seek)
//   7. TEARDOWN (释放通道)
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <thread>

static int pass_cnt = 0, fail_cnt = 0;
static void Check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) {
        ++pass_cnt;
        std::cout << "  [PASS] " << name << "\n";
    } else {
        ++fail_cnt;
        std::cout << "  [FAIL] " << name << (detail.empty() ? "" : (" -> " + detail)) << "\n";
    }
}

// 发送并接收 RTSP 文本响应
static std::string SendRtsp(int fd, const std::string& req) {
    if (send(fd, req.data(), req.size(), 0) != (ssize_t)req.size()) {
        return "";
    }

    std::string resp;
    char buf[4096];
    size_t content_len = 0;
    bool header_done = false;
    size_t header_len = 0;

    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        resp.append(buf, n);

        if (!header_done) {
            size_t pos = resp.find("\r\n\r\n");
            if (pos != std::string::npos) {
                header_done = true;
                header_len = pos + 4;
                // 查找 Content-Length
                size_t cl_pos = resp.find("Content-Length:");
                if (cl_pos == std::string::npos) cl_pos = resp.find("content-length:");
                if (cl_pos != std::string::npos && cl_pos < pos) {
                    content_len = std::strtoul(resp.c_str() + cl_pos + 15, nullptr, 10);
                }
            }
        }

        if (header_done && resp.size() >= header_len + content_len) {
            break;
        }
    }
    return resp;
}

int main(int argc, char** argv) {
    const char* ip = (argc > 1) ? argv[1] : "127.0.0.1";
    uint16_t port  = (argc > 2) ? (uint16_t)std::atoi(argv[2]) : 8554;
    std::string video_path = (argc > 3) ? argv[3] : "/tmp/test_vod.mp4";

    std::cout << "=== RtspClientTest -> " << ip << ":" << port << " path=" << video_path << " ===\n";

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Connect to RTSP server failed\n";
        return 1;
    }

    int cseq = 1;
    std::string url = "rtsp://" + std::string(ip) + ":" + std::to_string(port) + "/stream/" + video_path;

    // 1. OPTIONS
    std::cout << "\n[1. OPTIONS]\n";
    std::string req = "OPTIONS " + url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) + "\r\n\r\n";
    std::string resp = SendRtsp(fd, req);
    Check("OPTIONS 响应包含 200 OK", resp.find("200 OK") != std::string::npos);
    Check("包含 Public 方法支持", resp.find("Public:") != std::string::npos && resp.find("DESCRIBE") != std::string::npos);

    // 2. DESCRIBE
    std::cout << "\n[2. DESCRIBE]\n";
    req = "DESCRIBE " + url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) + "\r\nAccept: application/sdp\r\n\r\n";
    resp = SendRtsp(fd, req);
    Check("DESCRIBE 响应包含 200 OK", resp.find("200 OK") != std::string::npos);
    Check("SDP 包含 H264 视频轨道", resp.find("m=video") != std::string::npos && resp.find("H264") != std::string::npos);

    // 3. SETUP (为视频轨创建本地 UDP 端口)
    std::cout << "\n[3. SETUP]\n";
    int client_rtp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in client_rtp_addr{};
    client_rtp_addr.sin_family = AF_INET;
    client_rtp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    client_rtp_addr.sin_port = htons(30000);
    bind(client_rtp_sock, (sockaddr*)&client_rtp_addr, sizeof(client_rtp_addr));

    timeval tv{1, 0};
    setsockopt(client_rtp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::string track_url = url + "/trackID=0";
    req = "SETUP " + track_url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) +
          "\r\nTransport: RTP/AVP;unicast;client_port=30000-30001\r\n\r\n";
    resp = SendRtsp(fd, req);
    Check("SETUP 响应包含 200 OK", resp.find("200 OK") != std::string::npos);
    Check("SETUP 返回 Session", resp.find("Session:") != std::string::npos);

    // 提取 Session ID
    std::string session_id;
    size_t sess_pos = resp.find("Session:");
    if (sess_pos != std::string::npos) {
        size_t end_p = resp.find_first_of(";\r\n", sess_pos + 8);
        session_id = resp.substr(sess_pos + 8, end_p - (sess_pos + 8));
        while (!session_id.empty() && session_id.front() == ' ') session_id.erase(session_id.begin());
    }
    std::cout << "  session_id = " << session_id << "\n";

    // 4. PLAY
    std::cout << "\n[4. PLAY]\n";
    req = "PLAY " + url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) +
          "\r\nSession: " + session_id + "\r\nRange: npt=0.000-\r\n\r\n";
    resp = SendRtsp(fd, req);
    Check("PLAY 响应包含 200 OK", resp.find("200 OK") != std::string::npos);

    // 接收服务端推送的 RTP 数据包
    size_t rtp_pkt_count = 0;
    auto t_rtp = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_rtp).count() < 1500) {
        char rtp_buf[2048];
        ssize_t n = recv(client_rtp_sock, rtp_buf, sizeof(rtp_buf), 0);
        if (n >= 12) {
            uint8_t pt = rtp_buf[1] & 0x7F;
            if (pt == 96) { // H.264
                ++rtp_pkt_count;
            }
        }
    }
    Check("收到服务端 UDP 推送的 H.264 RTP 数据包", rtp_pkt_count > 0, "packets=" + std::to_string(rtp_pkt_count));
    std::cout << "  接收 RTP 包数量: " << rtp_pkt_count << "\n";

    // 5. PAUSE
    std::cout << "\n[5. PAUSE]\n";
    req = "PAUSE " + url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) +
          "\r\nSession: " + session_id + "\r\n\r\n";
    resp = SendRtsp(fd, req);
    Check("PAUSE 响应包含 200 OK", resp.find("200 OK") != std::string::npos);

    // 6. PLAY (Seek)
    std::cout << "\n[6. PLAY Seek]\n";
    req = "PLAY " + url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) +
          "\r\nSession: " + session_id + "\r\nRange: npt=1.000-\r\n\r\n";
    resp = SendRtsp(fd, req);
    Check("PLAY Seek 响应包含 200 OK", resp.find("200 OK") != std::string::npos);

    // 7. TEARDOWN
    std::cout << "\n[7. TEARDOWN]\n";
    req = "TEARDOWN " + url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) +
          "\r\nSession: " + session_id + "\r\n\r\n";
    resp = SendRtsp(fd, req);
    Check("TEARDOWN 响应包含 200 OK", resp.find("200 OK") != std::string::npos);

    // 8. 方案 B 实时桌面拉流验证: rtsp://ip:port/desktop
    std::cout << "\n[8. 方案 B 实时桌面推流 (/desktop)]\n";
    std::string desktop_url = "rtsp://" + std::string(ip) + ":" + std::to_string(port) + "/desktop";
    req = "DESCRIBE " + desktop_url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) + "\r\nAccept: application/sdp\r\n\r\n";
    resp = SendRtsp(fd, req);
    bool desk_describe_ok = (resp.find("200 OK") != std::string::npos && resp.find("m=video") != std::string::npos);
    Check("桌面推流 DESCRIBE 响应 200 OK 且包含视频轨", desk_describe_ok);

    if (desk_describe_ok) {
        req = "SETUP " + desktop_url + "/trackID=0 RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) +
              "\r\nTransport: RTP/AVP;unicast;client_port=30002-30003\r\n\r\n";
        resp = SendRtsp(fd, req);
        Check("桌面推流 SETUP 响应 200 OK", resp.find("200 OK") != std::string::npos);

        std::string desk_sess;
        size_t sp = resp.find("Session:");
        if (sp != std::string::npos) {
            size_t ep = resp.find_first_of(";\r\n", sp + 8);
            desk_sess = resp.substr(sp + 8, ep - (sp + 8));
            while (!desk_sess.empty() && desk_sess.front() == ' ') desk_sess.erase(desk_sess.begin());
        }

        req = "PLAY " + desktop_url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) +
              "\r\nSession: " + desk_sess + "\r\nRange: npt=0.000-\r\n\r\n";
        resp = SendRtsp(fd, req);
        Check("桌面推流 PLAY 响应 200 OK", resp.find("200 OK") != std::string::npos);

        // 停止推流
        req = "TEARDOWN " + desktop_url + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq++) +
              "\r\nSession: " + desk_sess + "\r\n\r\n";
        resp = SendRtsp(fd, req);
        Check("桌面推流 TEARDOWN 响应 200 OK", resp.find("200 OK") != std::string::npos);
    }

    close(client_rtp_sock);
    close(fd);

    std::cout << "\n========================================\n";
    std::cout << "RtspServer 测试结果: PASS=" << pass_cnt << " FAIL=" << fail_cnt << "\n";
    std::cout << "========================================\n";

    return (fail_cnt == 0 ? 0 : 2);
}
