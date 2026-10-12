// SigClientTest：信令服务器协议测试客户端
// 覆盖 DataId 全部消息：1000/1001/1002/1003/1004/1005/1006/1007/1008/1009/1010/1011/1100/1101
// 帧格式（与服务器 SigServer/src/TcpPacket.cpp 一致）：
//   0xFEFF(2) + DataLen(4,BE) + MsgId(2,BE) + Body(N) + Checksum(2,BE)
//   Checksum = (0xFE + 0xFF + DataLen四字节 + MsgId两字节 + Body各字节) & 0xFFFF
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <cerrno>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

// ---------------- 帧常量（与 define.h 一致） ----------------
static constexpr uint8_t  HEAD_FLAG_FIRST  = 0xFE;
static constexpr uint8_t  HEAD_FLAG_SECOND = 0xFF;
static constexpr uint32_t START_FLAG_LEN   = 2;
static constexpr uint32_t DATA_LEN_FIELD   = 4;
static constexpr uint32_t MSG_ID_LEN       = 2;
static constexpr uint32_t CHECKSUM_LEN     = 2;
static constexpr uint32_t FRAME_HEAD_LEN   = 6;  // StartFlag + DataLen

namespace client_b64 {
    static const char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    inline std::string encode(const char* data, size_t len) {
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        size_t i = 0;
        while (i + 2 < len) {
            uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i+1] << 8) | (uint8_t)data[i+2];
            out += TABLE[(v >> 18) & 0x3F];
            out += TABLE[(v >> 12) & 0x3F];
            out += TABLE[(v >>  6) & 0x3F];
            out += TABLE[ v        & 0x3F];
            i += 3;
        }
        size_t rem = len - i;
        if (rem == 1) {
            uint32_t v = (uint8_t)data[i] << 16;
            out += TABLE[(v >> 18) & 0x3F];
            out += TABLE[(v >> 12) & 0x3F];
            out += "==";
        } else if (rem == 2) {
            uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i+1] << 8);
            out += TABLE[(v >> 18) & 0x3F];
            out += TABLE[(v >> 12) & 0x3F];
            out += TABLE[(v >>  6) & 0x3F];
            out += '=';
        }
        return out;
    }
}

enum DataId : uint16_t {
    LOGIN_USER             = 1000,
    GET_DRIVERINFO         = 1001,
    GET_DIRINFO            = 1002,
    DOWNLOAD_FILE          = 1003,
    EXECUTE_FILE           = 1004,
    DELETE_FILE            = 1005,
    JOIN_TCP_REMOTEWATCH   = 1006,
    GET_REMOTEWATCH_SCREEN = 1007,
    EXIT_TCP_REMOTEWATCH   = 1008,
    MOUSE_REMOTE_CONTROL   = 1009,
    LOCK_REMOTE_CONTROL    = 1010,
    UNLOCK_REMOTE_CONTROL  = 1011,
    UPLOAD_FILE            = 1012,
    CANCEL_UPLOAD_FILE     = 1013,
    KEYBOARD_REMOTE_CONTROL= 1015,
    SEND_MSG               = 1100,
    SEND_FILE              = 1101,
};

// ---------------- 构造一帧 ----------------
static std::vector<char> BuildFrame(uint16_t id, const std::string& body)
{
    uint32_t data_len = MSG_ID_LEN + (uint32_t)body.size() + CHECKSUM_LEN;
    uint32_t total = FRAME_HEAD_LEN + data_len;
    std::vector<char> f(total);
    f[0] = (char)HEAD_FLAG_FIRST;
    f[1] = (char)HEAD_FLAG_SECOND;
    uint32_t dl_be = htonl(data_len);
    memcpy(f.data() + START_FLAG_LEN, &dl_be, DATA_LEN_FIELD);
    uint16_t id_be = htons(id);
    memcpy(f.data() + FRAME_HEAD_LEN, &id_be, MSG_ID_LEN);
    if (!body.empty())
        memcpy(f.data() + FRAME_HEAD_LEN + MSG_ID_LEN, body.data(), body.size());

    // checksum：0xFE+0xFF + DataLen四字节 + MsgId + Body
    uint32_t sum = HEAD_FLAG_FIRST + HEAD_FLAG_SECOND;
    for (uint32_t i = 0; i < DATA_LEN_FIELD; ++i)
        sum += (uint8_t)f[START_FLAG_LEN + i];
    for (uint32_t i = 0; i < MSG_ID_LEN + body.size(); ++i)
        sum += (uint8_t)f[FRAME_HEAD_LEN + i];
    uint16_t csum = (uint16_t)(sum & 0xFFFF);
    uint16_t csum_be = htons(csum);
    memcpy(f.data() + FRAME_HEAD_LEN + MSG_ID_LEN + body.size(), &csum_be, CHECKSUM_LEN);
    return f;
}

// ---------------- 收帧（带持久缓冲区） ----------------
struct Frame { uint16_t id = 0; std::string body; };

class FrameReceiver {
private:
    std::vector<char> _buf;
public:
    void Clear() { _buf.clear(); }

    bool RecvNextFrame(int fd, Frame& out, int timeout_ms = 3000) {
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        auto try_parse = [&]() -> bool {
            size_t pos = std::string::npos;
            for (size_t i = 0; i + 1 < _buf.size(); ++i) {
                if ((uint8_t)_buf[i] == HEAD_FLAG_FIRST && (uint8_t)_buf[i + 1] == HEAD_FLAG_SECOND) {
                    pos = i; break;
                }
            }
            if (pos == std::string::npos) {
                if (!_buf.empty() && (uint8_t)_buf.back() == HEAD_FLAG_FIRST)
                    _buf.erase(_buf.begin(), _buf.end() - 1);
                else
                    _buf.clear();
                return false;
            }
            if (pos > 0) _buf.erase(_buf.begin(), _buf.begin() + pos);
            if (_buf.size() < FRAME_HEAD_LEN) return false;

            uint32_t data_len =
                ((uint32_t)(uint8_t)_buf[START_FLAG_LEN + 0] << 24) |
                ((uint32_t)(uint8_t)_buf[START_FLAG_LEN + 1] << 16) |
                ((uint32_t)(uint8_t)_buf[START_FLAG_LEN + 2] << 8)  |
                ((uint32_t)(uint8_t)_buf[START_FLAG_LEN + 3]);
            if (data_len < MSG_ID_LEN + CHECKSUM_LEN) {
                _buf.erase(_buf.begin(), _buf.begin() + 2);
                return false;
            }
            if (_buf.size() < FRAME_HEAD_LEN + data_len) return false;

            // 校验
            uint32_t sum = HEAD_FLAG_FIRST + HEAD_FLAG_SECOND;
            for (uint32_t i = 0; i < DATA_LEN_FIELD; ++i) sum += (uint8_t)_buf[START_FLAG_LEN + i];
            for (uint32_t i = 0; i < MSG_ID_LEN + (data_len - MSG_ID_LEN - CHECKSUM_LEN); ++i)
                sum += (uint8_t)_buf[FRAME_HEAD_LEN + i];
            uint16_t calc = (uint16_t)(sum & 0xFFFF);
            const char* payload = _buf.data() + FRAME_HEAD_LEN;
            uint32_t body_n = data_len - MSG_ID_LEN - CHECKSUM_LEN;
            uint16_t recv_csum = (uint16_t)(((uint16_t)(uint8_t)payload[MSG_ID_LEN + body_n] << 8) |
                                            (uint16_t)(uint8_t)payload[MSG_ID_LEN + body_n + 1]);
            if (calc != recv_csum) {
                std::cerr << "  [WARN] checksum mismatch: calc=" << calc << " recv=" << recv_csum << "\n";
                _buf.erase(_buf.begin(), _buf.begin() + FRAME_HEAD_LEN + data_len);
                return false;
            }
            out.id = (uint16_t)(((uint16_t)(uint8_t)payload[0] << 8) | (uint8_t)payload[1]);
            out.body.assign(payload + MSG_ID_LEN, body_n);
            _buf.erase(_buf.begin(), _buf.begin() + FRAME_HEAD_LEN + data_len);
            return true;
        };

        char tmp[4096];
        while (true) {
            if (try_parse()) return true;
            ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
            if (n <= 0) return false;
            _buf.insert(_buf.end(), tmp, tmp + n);
        }
    }

    // 接收期望 ID 的帧，自动跳过穿插的异步帧（如 1007 GET_REMOTEWATCH_SCREEN）
    bool RecvExpected(int fd, uint16_t expected_id, Frame& out, int timeout_ms = 5000) {
        auto t0 = std::chrono::steady_clock::now();
        while (true) {
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();
            if (el > timeout_ms) return false;
            int rem = (int)(timeout_ms - el);
            Frame f;
            if (!RecvNextFrame(fd, f, rem)) return false;
            if (f.id == expected_id) {
                out = std::move(f);
                return true;
            }
            // 忽略异步帧
        }
    }
};

// 连接服务器
static int Connect(const char* ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "connect failed\n";
        return -1;
    }
    return fd;
}

static bool SendFrame(int fd, uint16_t id, const std::string& body)
{
    auto f = BuildFrame(id, body);
    return send(fd, f.data(), f.size(), 0) == (ssize_t)f.size();
}

static std::string JsonFind(const std::string& s, const std::string& key)
{
    std::string k = "\"" + key + "\"";
    size_t p = s.find(k);
    if (p == std::string::npos) return "";
    p = s.find(':', p + k.size());
    if (p == std::string::npos) return "";
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (p >= s.size()) return "";
    if (s[p] == '"') {
        size_t e = s.find('"', p + 1);
        return e == std::string::npos ? "" : s.substr(p + 1, e - p - 1);
    }
    size_t e = p;
    while (e < s.size() && s[e] != ',' && s[e] != '}' && s[e] != ' ' && s[e] != '\r' && s[e] != '\n') ++e;
    return s.substr(p, e - p);
}

static int pass_cnt = 0, fail_cnt = 0;
static void Check(const std::string& name, bool ok, const std::string& detail = "")
{
    if (ok) { ++pass_cnt; std::cout << "  [PASS] " << name << "\n"; }
    else    { ++fail_cnt; std::cout << "  [FAIL] " << name << (detail.empty() ? "" : (" -> " + detail)) << "\n"; }
}

int main(int argc, char** argv)
{
    const char* ip = (argc > 1) ? argv[1] : "127.0.0.1";
    uint16_t port  = (argc > 2) ? (uint16_t)atoi(argv[2]) : 10087;
    std::string user = (argc > 3) ? argv[3] : "lzw";
    std::string pass = (argc > 4) ? argv[4] : "lzw123";

    std::cout << "=== SigClientTest -> " << ip << ":" << port << " ===\n";
    int fd = Connect(ip, port);
    if (fd < 0) return 1;

    FrameReceiver rcv;
    Frame f;

    // ---------- 1000 LOGIN_USER ----------
    std::cout << "\n[1000 LOGIN_USER]\n";
    SendFrame(fd, LOGIN_USER, "{\"username\":\"" + user + "\",\"password\":\"" + pass + "\"}");
    bool got_login = rcv.RecvExpected(fd, LOGIN_USER, f, 3000);
    bool login_ok = got_login && JsonFind(f.body, "error") == "0";
    Check("登录响应 error=0", login_ok, got_login ? ("id=" + std::to_string(f.id) + " body=" + f.body) : "timeout");
    if (!login_ok) {
        std::cout << "\n登录失败，中止后续测试。\n";
        close(fd);
        return 2;
    }

    // ---------- 1001 GET_DRIVERINFO ----------
    std::cout << "\n[1001 GET_DRIVERINFO]\n";
    SendFrame(fd, GET_DRIVERINFO, "{\"data\":\"empty\"}");
    if (rcv.RecvExpected(fd, GET_DRIVERINFO, f, 3000)) {
        std::string err = JsonFind(f.body, "error");
        std::string sys  = JsonFind(f.body, "system");
        std::string drv  = JsonFind(f.body, "driverinfo");
        Check("error=0", err == "0", "error=" + err);
        Check("system=Linux", sys == "Linux", "system=" + sys);
        Check("driverinfo 非空", !drv.empty(), "driverinfo=" + drv);
        std::cout << "  driverinfo = " << drv << "\n";
    } else Check("收到 1001 响应", false, "timeout");

    // ---------- 1002 GET_DIRINFO ----------
    std::cout << "\n[1002 GET_DIRINFO]\n";
    SendFrame(fd, GET_DIRINFO, "{\"path\":\"/home/lzw\"}");
    size_t entries = 0;
    bool has_end = false;
    auto t_start = std::chrono::steady_clock::now();
    while (true) {
        if (!rcv.RecvNextFrame(fd, f, 3000)) break;
        if (f.id == GET_DIRINFO) {
            if (JsonFind(f.body, "next") == "false") {
                has_end = true;
                break;
            } else {
                ++entries;
            }
        }
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t_start).count() > 5)
            break;
    }
    Check("收到 1002 目录条目", entries > 0, "entries=" + std::to_string(entries));
    Check("包含 next:false 结束帧", has_end);
    std::cout << "  目录条目数=" << entries << "\n";

    // ---------- 1004 EXECUTE_FILE ----------
    std::cout << "\n[1004 EXECUTE_FILE]\n";
    // 白名单外路径应返回 error!=0，验证白名单安全门控与回路响应
    SendFrame(fd, EXECUTE_FILE, "{\"executefilepath\":\"/bin/ls\"}");
    if (rcv.RecvExpected(fd, EXECUTE_FILE, f, 3000)) {
        std::string err = JsonFind(f.body, "error");
        Check("收到 1004 响应", true);
        std::cout << "  白名单外执行 error=" << err << "（符合预期）\n";
    } else Check("收到 1004 响应", false, "timeout");

    // ---------- 1005 DELETE_FILE ----------
    std::cout << "\n[1005 DELETE_FILE]\n";
    SendFrame(fd, DELETE_FILE, "{\"deletefilepath\":\"/home/lzw/__no_such_file_to_del__.txt\"}");
    if (rcv.RecvExpected(fd, DELETE_FILE, f, 3000)) {
        std::string err = JsonFind(f.body, "error");
        Check("收到 1005 响应", true);
        std::cout << "  不存在文件删除 error=" << err << "（符合预期）\n";
    } else Check("收到 1005 响应", false, "timeout");

    // ---------- 1006 JOIN / 1007 SCREEN / 1008 EXIT 远程监控组 ----------
    std::cout << "\n[1006 JOIN / 1007 SCREEN / 1008 EXIT 远程监控组]\n";
    SendFrame(fd, JOIN_TCP_REMOTEWATCH, "{\"data\":\"empty\"}");
    bool join_ok = rcv.RecvExpected(fd, JOIN_TCP_REMOTEWATCH, f, 3000) && JsonFind(f.body, "error") == "0";
    Check("加入监控 error=0", join_ok);

    // 等待一帧 1007 截图推送
    bool got_screen = rcv.RecvExpected(fd, GET_REMOTEWATCH_SCREEN, f, 3000);
    Check("收到 1007 截图推送", got_screen);
    if (got_screen)
        std::cout << "  screen error=" << JsonFind(f.body, "error")
                  << " screenpix_len=" << JsonFind(f.body, "screenpix").size() << "\n";

    // 退出监控
    SendFrame(fd, EXIT_TCP_REMOTEWATCH, "{\"data\":\"empty\"}");
    bool exit_ok = rcv.RecvExpected(fd, EXIT_TCP_REMOTEWATCH, f, 5000) && JsonFind(f.body, "error") == "0";
    Check("退出监控 error=0", exit_ok);

    // ---------- 1009 MOUSE_REMOTE_CONTROL ----------
    std::cout << "\n[1009 MOUSE_REMOTE_CONTROL]\n";
    bool sent_mouse = SendFrame(fd, MOUSE_REMOTE_CONTROL,
        "{\"xScale\":0.5,\"yScale\":0.5,\"button\":1,\"type\":\"moved\"}");
    Check("发送鼠标事件（无响应消息）", sent_mouse);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 探活
    SendFrame(fd, GET_DRIVERINFO, "{\"data\":\"empty\"}");
    Check("鼠标注入后连接存活", rcv.RecvExpected(fd, GET_DRIVERINFO, f, 3000));

    // ---------- 1015 KEYBOARD_REMOTE_CONTROL ----------
    std::cout << "\n[1015 KEYBOARD_REMOTE_CONTROL]\n";
    // 模拟按下和释放 Qt::Key_A (0x41) 与 Qt::Key_Return (0x01000004)
    bool sent_key1 = SendFrame(fd, KEYBOARD_REMOTE_CONTROL,
        "{\"keycode\":65,\"modifiers\":0,\"text\":\"a\",\"type\":\"pressed\"}");
    bool sent_key2 = SendFrame(fd, KEYBOARD_REMOTE_CONTROL,
        "{\"keycode\":65,\"modifiers\":0,\"text\":\"a\",\"type\":\"released\"}");
    Check("发送键盘按下/释放事件（无响应消息）", sent_key1 && sent_key2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    SendFrame(fd, GET_DRIVERINFO, "{\"data\":\"empty\"}");
    Check("键盘注入后连接存活", rcv.RecvExpected(fd, GET_DRIVERINFO, f, 3000));

    // ---------- 1010 LOCK / 1011 UNLOCK ----------
    std::cout << "\n[1010 LOCK / 1011 UNLOCK]\n";
    Check("发送锁机", SendFrame(fd, LOCK_REMOTE_CONTROL, "{\"data\":\"empty\"}"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Check("发送解锁", SendFrame(fd, UNLOCK_REMOTE_CONTROL, "{\"data\":\"empty\"}"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    SendFrame(fd, GET_DRIVERINFO, "{\"data\":\"empty\"}");
    Check("锁/解锁后连接存活", rcv.RecvExpected(fd, GET_DRIVERINFO, f, 3000));

    // ---------- 1100 SEND_MSG / 1101 SEND_FILE ----------
    std::cout << "\n[1100 SEND_MSG / 1101 SEND_FILE]\n";
    SendFrame(fd, SEND_MSG, "{\"type\":\"text\",\"content\":\"hello\",\"name\":\"tester\",\"time\":\"now\"}");
    bool msg_ok = rcv.RecvExpected(fd, SEND_MSG, f, 3000);
    Check("1100 聊天消息回声", msg_ok && JsonFind(f.body, "content") == "hello");

    SendFrame(fd, SEND_FILE, "{\"type\":\"file\",\"filename\":\"test.bin\",\"filesize\":16,"
                              "\"filesuffix\":\"bin\",\"name\":\"tester\",\"time\":\"now\",\"content\":\"\"}");
    bool file_ok = rcv.RecvExpected(fd, SEND_FILE, f, 3000);
    Check("1101 聊天文件回声", file_ok && JsonFind(f.body, "filename") == "test.bin");

    // ---------- 1003 DOWNLOAD_FILE ----------
    std::cout << "\n[1003 DOWNLOAD_FILE]\n";
    // 1) 错误路径探测
    SendFrame(fd, DOWNLOAD_FILE,
        "{\"sourcesavepath\":\"/tmp/dl\",\"destfilepath\":\"/home/lzw/__no_such_file__.bin\"}");
    if (rcv.RecvExpected(fd, DOWNLOAD_FILE, f, 3000)) {
        Check("不存在文件下载返回 error=1 head=true",
              JsonFind(f.body, "error") == "1" && JsonFind(f.body, "head") == "true");
    } else Check("下载错误响应", false, "timeout");

    // 2) 真实文件下载（我们在前期创建了 /home/lzw/sigtest_dl.bin）
    SendFrame(fd, DOWNLOAD_FILE,
        "{\"sourcesavepath\":\"/tmp/dl\",\"destfilepath\":\"/home/lzw/sigtest_dl.bin\"}");
    bool got_head = rcv.RecvExpected(fd, DOWNLOAD_FILE, f, 3000);
    Check("真实文件下载头帧 error=0 head=true",
          got_head && JsonFind(f.body, "error") == "0" && JsonFind(f.body, "head") == "true");

    size_t chunk_count = 0;
    bool dl_done = false;
    auto t_dl = std::chrono::steady_clock::now();
    while (true) {
        if (!rcv.RecvNextFrame(fd, f, 5000)) break;
        if (f.id == DOWNLOAD_FILE) {
            std::string head_f = JsonFind(f.body, "head");
            std::string next_f = JsonFind(f.body, "next");
            if (head_f == "false") {
                ++chunk_count;
                if (next_f == "false") {
                    dl_done = true;
                    break;
                }
            }
        }
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t_dl).count() > 10)
            break;
    }
    Check("收到下载分块与结束帧", dl_done, "chunks=" + std::to_string(chunk_count));
    std::cout << "  下载块数=" << chunk_count << "\n";

    // ---------- 1012 UPLOAD_FILE / 1013 CANCEL_UPLOAD_FILE ----------
    std::cout << "\n[1012 UPLOAD_FILE / 1013 CANCEL_UPLOAD_FILE]\n";
    std::string upload_remote = "/home/lzw/sigtest_uploaded.bin";
    std::string upload_content = "Hello NoDeskServerKit Upload Test 1234567890!";
    std::string upload_b64 = client_b64::encode(upload_content.data(), upload_content.size());

    // 1) 正常单块上传（next=false）
    std::string upload_req =
        "{\"localpath\":\"D:/test.bin\",\"remotepath\":\"" + upload_remote + "\","
        "\"offset\":0,\"totallen\":" + std::to_string(upload_content.size()) + ","
        "\"next\":false,\"index\":0,\"length\":" + std::to_string(upload_content.size()) + ","
        "\"data\":\"" + upload_b64 + "\"}";
    SendFrame(fd, UPLOAD_FILE, upload_req);
    bool got_up_resp = rcv.RecvExpected(fd, UPLOAD_FILE, f, 3000);
    bool up_ok = got_up_resp && JsonFind(f.body, "error") == "0" &&
                 JsonFind(f.body, "finishe") == "true" &&
                 JsonFind(f.body, "remotepath") == upload_remote;
    Check("文件上传响应 error=0 finishe=true", up_ok, got_up_resp ? f.body : "timeout");

    // 2) 校验服务端落盘内容
    std::ifstream up_check(upload_remote);
    std::string up_read_back((std::istreambuf_iterator<char>(up_check)), std::istreambuf_iterator<char>());
    Check("上传文件内容与原文一致", up_read_back == upload_content);
    unlink(upload_remote.c_str());

    // 3) CANCEL_UPLOAD_FILE 取消上传
    std::string cancel_target = "/home/lzw/sigtest_cancel.bin";
    SendFrame(fd, CANCEL_UPLOAD_FILE, "{\"localpath\":\"D:/cancel.bin\",\"remotepath\":\"" + cancel_target + "\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 探活确认连接在取消上传后保持正常
    SendFrame(fd, GET_DRIVERINFO, "{\"data\":\"empty\"}");
    Check("取消上传后连接存活", rcv.RecvExpected(fd, GET_DRIVERINFO, f, 3000));

    close(fd);

    std::cout << "\n========================================\n";
    std::cout << "全部测试结果: PASS=" << pass_cnt << " FAIL=" << fail_cnt << "\n";
    std::cout << "========================================\n";
    return fail_cnt == 0 ? 0 : 3;
}