#include "SigLogicSystem.h"
#include "jsoncpp/json/json.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

// X11 鼠标注入（规范 §3.8）
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

// ==================== 工具：JSON 输出（紧凑格式） ====================
static std::string ToJsonStr(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";  // 紧凑输出（QJsonDocument::toJson 默认紧凑，与客户端匹配）
    return Json::writeString(wb, v);
}

// ==================== 登录鉴权（规范 §3.1）====================
// 校验 Linux 系统账号：优先 getspnam + crypt（需 root），回退 getpwnam
//
// 权限说明：/etc/shadow 仅 root:shadow 可读，非 root 进程 getspnam() 返回 NULL，
// 且 passwd 字段恒为 "x"，导致无法完成密码校验（规范要求鉴权必须做）。
// 因此鉴权分三档：
//   1) 非 root 且设置了测试凭证环境变量 SIG_TEST_USER / SIG_TEST_PASS：
//      仅当输入与该凭证完全一致时放行（用于开发/CI 环境联调，生产应以 root 运行）。
//   2) root 运行：走 shadow + crypt 系统账号鉴权（规范正式路径）。
//   3) 其余情况：按系统账号解析，返回对应 LoginStatus 错误码。
static int AuthenticateUser(const std::string& username, const std::string& password)
{
    if (username.empty() || password.empty())
        return (int)LoginStatus::LOGIN_USERNAME_ERROR;

    const bool is_root = (geteuid() == 0);

    // 档位 1：非 root 开发环境测试凭证
    if (!is_root) {
        const char* tu = std::getenv("SIG_TEST_USER");
        const char* tp = std::getenv("SIG_TEST_PASS");
        if (tu && tp && *tu && *tp) {
            if (username == tu && password == tp)
                return (int)LoginStatus::LOGIN_SUCCESS;
            // 用户名匹配但密码不符 → 密码错误；否则按用户不存在处理
            return (username == tu)
                ? (int)LoginStatus::LOGIN_PASSWORD_ERROR
                : (int)LoginStatus::LOGIN_USERNAME_ERROR;
        }
        // 未配置测试凭证时，非 root 无法读取 shadow，明确返回系统错误
        std::cerr << "[Auth] 非 root 运行且未配置 SIG_TEST_USER/SIG_TEST_PASS，"
                  << "无法校验系统密码（请以 root 运行或设置测试凭证）" << std::endl;
        return (int)LoginStatus::LOGIN_SYSTEM_ERROR;
    }

    // 档位 2：root 走系统鉴权
    // 2a) shadow（需要 root 权限）
    struct spwd* sp = getspnam(username.c_str());
    if (sp != nullptr && sp->sp_pwdp != nullptr) {
        std::string hash(sp->sp_pwdp);
        if (hash == "!" || hash == "*" || hash.empty())
            return (int)LoginStatus::LOGIN_USERNAME_ERROR;
        char* enc = crypt(password.c_str(), hash.c_str());
        if (enc == nullptr)
            return (int)LoginStatus::LOGIN_SYSTEM_ERROR;
        if (hash == enc)
            return (int)LoginStatus::LOGIN_SUCCESS;
        return (int)LoginStatus::LOGIN_PASSWORD_ERROR;
    }

    // 2b) 回退 passwd（如密码在 passwd 字段，或 shadow 不可读）
    struct passwd* pw = getpwnam(username.c_str());
    if (pw == nullptr)
        return (int)LoginStatus::LOGIN_USERNAME_ERROR;  // 用户不存在

    std::string hash(pw->pw_passwd ? pw->pw_passwd : "");
    if (hash.empty() || hash == "x" || hash == "!")
        return (int)LoginStatus::LOGIN_SYSTEM_ERROR;    // 无法读取密码哈希

    char* enc = crypt(password.c_str(), hash.c_str());
    if (enc == nullptr)
        return (int)LoginStatus::LOGIN_SYSTEM_ERROR;
    if (hash == enc)
        return (int)LoginStatus::LOGIN_SUCCESS;
    return (int)LoginStatus::LOGIN_PASSWORD_ERROR;
}

// ==================== 系统用户名 → 家目录 ====================
static std::string GetUserHome(const std::string& username)
{
    struct passwd* pw = getpwnam(username.c_str());
    if (pw && pw->pw_dir && pw->pw_dir[0] != '\0')
        return std::string(pw->pw_dir);
    return "/home/" + username;  // 兜底
}

// ==================== 文件类型（用于 GET_DIRINFO type 字段）====================
static std::string FileTypeName(mode_t mode)
{
    if (S_ISDIR(mode))  return "directory";
    if (S_ISREG(mode))  return "file";
    return "other";
}

// 权限字符串 "-rwxr-xr-x"
static std::string ModeToString(mode_t mode)
{
    char buf[11];
    buf[0] = S_ISDIR(mode) ? 'd' : (S_ISLNK(mode) ? 'l' : '-');
    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (mode & S_IXUSR) ? 'x' : '-';
    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (mode & S_IXGRP) ? 'x' : '-';
    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (mode & S_IXOTH) ? 'x' : '-';
    buf[10] = '\0';
    return std::string(buf);
}

// 时间格式化 "YYYY-MM-DD HH:MM:SS"
static std::string TimeToString(time_t t)
{
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

// ==================== 目录项信息（用于 GET_DIRINFO）====================
struct DirEntryInfo {
    std::string name;
    std::string type;       // "directory" / "file" / "other"
    long long size = 0;
    std::string permissions;
    std::string access_time;
    std::string symlink_target;
};

// 读取目录下所有条目（先目录后文件）
static bool ListDirectory(const std::string& path, std::vector<DirEntryInfo>& dirs,
                          std::vector<DirEntryInfo>& files)
{
    DIR* dp = opendir(path.c_str());
    if (!dp) return false;

    struct dirent* de;
    while ((de = readdir(dp)) != nullptr) {
        std::string name = de->d_name;
        if (name == "." || name == "..") continue;

        std::string full = path + "/" + name;
        struct stat st;
        // 用 lstat 以识别软链接本身
        if (lstat(full.c_str(), &st) != 0) continue;

        DirEntryInfo info;
        info.name = name;
        info.permissions = ModeToString(st.st_mode);
        info.access_time = TimeToString(st.st_atime);

        if (S_ISLNK(st.st_mode)) {
            // 软链接：记录目标
            char target[PATH_MAX];
            ssize_t len = readlink(full.c_str(), target, sizeof(target) - 1);
            if (len > 0) {
                target[len] = '\0';
                info.symlink_target = target;
            }
            // type 以链接目标为准
            struct stat lst;
            if (stat(full.c_str(), &lst) == 0)
                info.type = FileTypeName(lst.st_mode);
            else
                info.type = "other";
        } else {
            info.symlink_target = "";
            info.type = FileTypeName(st.st_mode);
        }

        if (info.type == "directory") {
            // 目录 size = 子项数量
            DIR* sub = opendir(full.c_str());
            long long cnt = 0;
            if (sub) {
                struct dirent* sde;
                while ((sde = readdir(sub)) != nullptr) {
                    std::string sn = sde->d_name;
                    if (sn != "." && sn != "..") ++cnt;
                }
                closedir(sub);
            }
            info.size = cnt;
            dirs.push_back(std::move(info));
        } else {
            info.size = (long long)st.st_size;
            files.push_back(std::move(info));
        }
    }
    closedir(dp);
    return true;
}

// ==================== 文件执行（规范 §3.5）====================
// 返回 0 成功，非 0 失败
static int ExecuteFile(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return 1;

    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        // 子进程：脱离父进程，setsid 后 exec
        setsid();
        // 若是脚本或可执行，直接 exec
        execl(path.c_str(), path.c_str(), (char*)nullptr);
        // exec 失败
        _exit(127);
    }
    // 父进程：不等待（异步执行），返回成功
    return 0;
}

// ==================== 删除文件/目录（规范 §3.6）====================
static int DeletePath(const std::string& path)
{
    struct stat st;
    if (lstat(path.c_str(), &st) != 0)
        return 1;
    if (S_ISDIR(st.st_mode)) {
        // 非空目录删除（递归）
        DIR* dp = opendir(path.c_str());
        if (!dp) return 1;
        struct dirent* de;
        while ((de = readdir(dp)) != nullptr) {
            std::string n = de->d_name;
            if (n == "." || n == "..") continue;
            DeletePath(path + "/" + n);
        }
        closedir(dp);
        return rmdir(path.c_str()) == 0 ? 0 : 1;
    } else {
        return unlink(path.c_str()) == 0 ? 0 : 1;
    }
}

// ==================== 注册全部消息处理器 ====================
void SigLogicSystem::init_id_function()
{
    // ---------------------------------------------------------------
    // 1000 LOGIN_USER —— 登录鉴权（规范 §3.1）
    // ---------------------------------------------------------------
    id_func_map[DataId::LOGIN_USER] =
        [](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (!body) return;
        Json::Value j;
        Json::CharReaderBuilder rb;
        std::string perr;
        std::string js(body.get(), body.get() + strlen(body.get()));
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(js.data(), js.data() + js.size(), &j, &perr)) return;
        if (!j.isMember("username") || !j.isMember("password")) return;

        std::string username = j["username"].asString();
        std::string password = j["password"].asString();

        int errCode = AuthenticateUser(username, password);
        std::cerr << "[LOGIN] user=" << username << " errCode=" << errCode
                  << " euid=" << geteuid() << std::endl;   // 调试
        Json::Value resp;
        resp["error"] = errCode;
        resp["username"] = username;
        conn->SendJson(DataId::LOGIN_USER, ToJsonStr(resp));

        if (errCode == (int)LoginStatus::LOGIN_SUCCESS) {
            conn->SetUsername(username);
            conn->SetHomeDir(GetUserHome(username));
            conn->SetState(ConnState::AUTHED);
            std::cout << "[LOGIN] user=" << username << " home=" << conn->HomeDir() << std::endl;
        } else {
            // 规范：失败立即断开
            conn->Close();
        }
    };

    // ---------------------------------------------------------------
    // 1001 GET_DRIVERINFO —— 获取磁盘分区/根目录（规范 §3.2）
    // ---------------------------------------------------------------
    id_func_map[DataId::GET_DRIVERINFO] =
        [](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;

        Json::Value resp;
        resp["error"] = 0;
        resp["system"] = "Linux";
        // Linux 单分区：返回登录用户家目录
        resp["driverinfo"] = conn->HomeDir();
        conn->SendJson(DataId::GET_DRIVERINFO, ToJsonStr(resp));
    };

    // ---------------------------------------------------------------
    // 1002 GET_DIRINFO —— 获取目录内容（逐条推送 + 结束帧）（规范 §3.3）
    // ---------------------------------------------------------------
    id_func_map[DataId::GET_DIRINFO] =
        [](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;
        if (!body) return;

        Json::Value j;
        Json::CharReaderBuilder rb;
        std::string perr;
        std::string js(body.get(), body.get() + strlen(body.get()));
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(js.data(), js.data() + js.size(), &j, &perr)) return;
        if (!j.isMember("path")) return;

        std::string path = j["path"].asString();
        // 路径白名单校验
        std::string real = conn->SafeRealPath(path);
        if (real.empty()) {
            // 路径非法：发结束帧
            Json::Value resp;
            resp["error"] = 1;
            resp["parent_path"] = path;
            resp["next"] = false;
            conn->SendJson(DataId::GET_DIRINFO, ToJsonStr(resp));
            return;
        }

        std::vector<DirEntryInfo> dirs, files;
        if (!ListDirectory(real, dirs, files)) {
            Json::Value resp;
            resp["error"] = 1;
            resp["parent_path"] = real;
            resp["next"] = false;
            conn->SendJson(DataId::GET_DIRINFO, ToJsonStr(resp));
            return;
        }

        // 先发所有目录条目，再发所有文件条目（规范建议顺序）
        auto send_entry = [&](const DirEntryInfo& e){
            Json::Value resp;
            resp["error"] = 0;
            resp["next"] = true;
            resp["parent_path"] = real;
            resp["name"] = e.name;
            resp["type"] = e.type;
            resp["size"] = (Json::Int64)e.size;
            resp["permissions"] = e.permissions;
            resp["access_time"] = e.access_time;
            resp["symlink_target"] = e.symlink_target;
            conn->SendJson(DataId::GET_DIRINFO, ToJsonStr(resp));
        };
        for (const auto& e : dirs)  send_entry(e);
        for (const auto& e : files) send_entry(e);

        // 结束帧（空目录也发）
        Json::Value done;
        done["error"] = 0;
        done["parent_path"] = real;
        done["next"] = false;
        conn->SendJson(DataId::GET_DIRINFO, ToJsonStr(done));
    };

    // ---------------------------------------------------------------
    // 1003 DOWNLOAD_FILE —— 文件下载：头帧 + 512KB 分块（规范 §3.4）
    //   注：此处同步循环发送（WriteBuf 队列由 EPOLLOUT 异步发送），
    //   生产环境可改为每块入队 + EPOLLOUT 回调推进（设计文档推荐）。
    // ---------------------------------------------------------------
    id_func_map[DataId::DOWNLOAD_FILE] =
        [](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;
        if (!body) return;

        Json::Value j;
        Json::CharReaderBuilder rb;
        std::string perr;
        std::string js(body.get(), body.get() + strlen(body.get()));
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(js.data(), js.data() + js.size(), &j, &perr)) return;
        if (!j.isMember("sourcesavepath") || !j.isMember("destfilepath")) return;

        std::string sourcesavepath = j["sourcesavepath"].asString();
        std::string destfilepath    = j["destfilepath"].asString();

        // 路径白名单校验
        std::string real = conn->SafeRealPath(destfilepath);
        if (real.empty()) {
            Json::Value resp;
            resp["error"] = 1;
            resp["head"] = true;
            resp["sourcesavepath"] = sourcesavepath;
            resp["destfilepath"] = destfilepath;
            resp["length"] = (Json::Int64)0;
            resp["totallen"] = (Json::Int64)0;
            resp["next"] = false;
            resp["index"] = 0;
            conn->SendJson(DataId::DOWNLOAD_FILE, ToJsonStr(resp));
            return;
        }

        // 打开文件
        std::ifstream ifs(real, std::ios::binary | std::ios::ate);
        if (!ifs) {
            Json::Value resp;
            resp["error"] = 1;
            resp["head"] = true;
            resp["sourcesavepath"] = sourcesavepath;
            resp["destfilepath"] = destfilepath;
            resp["length"] = (Json::Int64)0;
            resp["totallen"] = (Json::Int64)0;
            resp["next"] = false;
            resp["index"] = 0;
            conn->SendJson(DataId::DOWNLOAD_FILE, ToJsonStr(resp));
            return;
        }

        uint64_t file_size = (uint64_t)ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        // 发送文件头帧
        {
            Json::Value resp;
            resp["error"] = 0;
            resp["head"] = true;
            resp["sourcesavepath"] = sourcesavepath;
            resp["destfilepath"] = destfilepath;
            resp["length"] = (Json::Int64)0;
            resp["totallen"] = (Json::Int64)file_size;
            resp["next"] = true;
            resp["index"] = 0;
            conn->SendJson(DataId::DOWNLOAD_FILE, ToJsonStr(resp));
        }

        // 关键流控：文件头后延迟（规范建议 100~300ms），等客户端建进度条
        std::this_thread::sleep_for(std::chrono::milliseconds(DOWNLOAD_HEAD_DELAY_MS));

        // 循环读取 512KB 分块发送
        uint64_t read_size = 0;
        uint32_t index = 1;
        std::vector<char> buf(DOWNLOAD_CHUNK_SIZE);

        while (read_size < file_size) {
            uint64_t remain = file_size - read_size;
            uint32_t chunk = (uint32_t)std::min<uint64_t>(DOWNLOAD_CHUNK_SIZE, remain);
            ifs.read(buf.data(), chunk);
            uint32_t actual = (uint32_t)ifs.gcount();
            if (actual == 0) break;
            read_size += actual;

            std::string b64 = base64::encode(buf.data(), actual);
            bool is_last = (read_size >= file_size);

            Json::Value resp;
            resp["error"] = 0;
            resp["head"] = false;
            resp["sourcesavepath"] = sourcesavepath;
            resp["destfilepath"] = destfilepath;
            resp["length"] = (Json::Int64)actual;
            resp["totallen"] = (Json::Int64)file_size;
            resp["data"] = b64;
            resp["next"] = !is_last;
            resp["index"] = (int)index;
            conn->SendJson(DataId::DOWNLOAD_FILE, ToJsonStr(resp));

            ++index;
        }

        // 结束帧（next=false）
        {
            Json::Value resp;
            resp["error"] = 0;
            resp["head"] = false;
            resp["sourcesavepath"] = sourcesavepath;
            resp["destfilepath"] = destfilepath;
            resp["length"] = (Json::Int64)0;
            resp["totallen"] = (Json::Int64)file_size;
            resp["data"] = "";
            resp["next"] = false;
            resp["index"] = (int)index;
            conn->SendJson(DataId::DOWNLOAD_FILE, ToJsonStr(resp));
        }
    };

    // ---------------------------------------------------------------
    // 1004 EXECUTE_FILE —— 远程执行（规范 §3.5）
    // ---------------------------------------------------------------
    id_func_map[DataId::EXECUTE_FILE] =
        [](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;
        if (!body) return;

        Json::Value j;
        Json::CharReaderBuilder rb;
        std::string perr;
        std::string js(body.get(), body.get() + strlen(body.get()));
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(js.data(), js.data() + js.size(), &j, &perr)) return;
        if (!j.isMember("executefilepath")) return;

        std::string path = j["executefilepath"].asString();
        std::string real = conn->SafeRealPath(path);
        int errCode = real.empty() ? 1 : ExecuteFile(real);

        Json::Value resp;
        resp["error"] = errCode;
        resp["executefilepath"] = path;
        conn->SendJson(DataId::EXECUTE_FILE, ToJsonStr(resp));
    };

    // ---------------------------------------------------------------
    // 1005 DELETE_FILE —— 远程删除（规范 §3.6）
    // ---------------------------------------------------------------
    id_func_map[DataId::DELETE_FILE] =
        [](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;
        if (!body) return;

        Json::Value j;
        Json::CharReaderBuilder rb;
        std::string perr;
        std::string js(body.get(), body.get() + strlen(body.get()));
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(js.data(), js.data() + js.size(), &j, &perr)) return;
        if (!j.isMember("deletefilepath")) return;

        std::string path = j["deletefilepath"].asString();
        std::string real = conn->SafeRealPath(path);
        int errCode = real.empty() ? 1 : DeletePath(real);

        Json::Value resp;
        resp["error"] = errCode;
        resp["deletefilepath"] = path;
        conn->SendJson(DataId::DELETE_FILE, ToJsonStr(resp));
    };

    // ---------------------------------------------------------------
    // 1006 JOIN_TCP_REMOTEWATCH —— 加入远程监控（规范 §3.7 方案 B）
    // 客户端期望返回: { error: int, msg: string }
    // 探测桌面环境：
    //   - 无桌面 (无 $DISPLAY 或 XOpenDisplay 失败) -> error: 1 (WATCH_NO_DESKTOP)
    //   - 有桌面 -> error: 0 (WATCH_SUCCESS)，客户端启动 rtsp://ip:8554/desktop 拉流
    // ---------------------------------------------------------------
    id_func_map[DataId::JOIN_TCP_REMOTEWATCH] =
        [this](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;

        int status = (int)RemoteWatchStatus::WATCH_SUCCESS;
        std::string msg = "";

        display_mutex.lock();
        if (!display) {
            display = XOpenDisplay(NULL);
        }
        bool has_x11 = (display != nullptr);
        display_mutex.unlock();

        const char* disp_env = std::getenv("DISPLAY");
        if (!has_x11 || !disp_env || *disp_env == '\0') {
            status = (int)RemoteWatchStatus::WATCH_NO_DESKTOP;
            msg = "服务器处于无桌面环境（Headless），远程监控不可用";
        } else {
            add_remotewatch_client(conn);
        }

        Json::Value resp;
        resp["error"] = status;
        resp["msg"] = msg;
        conn->SendJson(DataId::JOIN_TCP_REMOTEWATCH, ToJsonStr(resp));
    };

    // ---------------------------------------------------------------
    // 1008 EXIT_TCP_REMOTEWATCH —— 退出远程监控组（规范 §3.7）
    // ---------------------------------------------------------------
    id_func_map[DataId::EXIT_TCP_REMOTEWATCH] =
        [this](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        delete_remotewatch_client(conn);

        Json::Value resp;
        resp["error"] = 0;
        resp["msg"] = "";
        conn->SendJson(DataId::EXIT_TCP_REMOTEWATCH, ToJsonStr(resp));
    };

    // ---------------------------------------------------------------
    // 1009 MOUSE_REMOTE_CONTROL —— 远程鼠标注入（规范 §3.8，无需回复）
    // ---------------------------------------------------------------
    id_func_map[DataId::MOUSE_REMOTE_CONTROL] =
        [this](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;
        if (!body) return;

        Json::Value j;
        Json::CharReaderBuilder rb;
        std::string perr;
        std::string js(body.get(), body.get() + strlen(body.get()));
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(js.data(), js.data() + js.size(), &j, &perr)) return;
        if (!j.isMember("xScale") || !j.isMember("yScale") || !j.isMember("type")) return;

        double xScale = j["xScale"].asDouble();
        double yScale = j["yScale"].asDouble();
        std::string type = j["type"].asString();
        int button = j.isMember("button") ? j["button"].asInt() : 0;

        std::lock_guard<std::mutex> lock(display_mutex);
        if (!display) return;

        int scr = DefaultScreen(display);
        int w = DisplayWidth(display, scr);
        int h = DisplayHeight(display, scr);
        int x = (int)(xScale * w);
        int y = (int)(yScale * h);

        // Qt 按键 → X11 按键映射
        auto qt_to_x11 = [](int b) -> int {
            switch (b) {
                case 1:  return 1;  // Left
                case 2:  return 3;  // Right
                case 4:  return 2;  // Middle
                case 8:  return 8;  // X1
                case 16: return 9;  // X2
                default: return 0;
            }
        };

        if (type == "pressed" || type == "released") {
            XTestFakeMotionEvent(display, 0, x, y, CurrentTime);
            XFlush(display);
            int xb = qt_to_x11(button);
            if (xb != 0) {
                XTestFakeButtonEvent(display, xb, type == "pressed", CurrentTime);
                XFlush(display);
            }
        } else if (type == "moved") {
            XTestFakeMotionEvent(display, 0, x, y, CurrentTime);
            XFlush(display);
        } else if (type == "dblclick") {
            XTestFakeMotionEvent(display, 0, x, y, CurrentTime);
            XFlush(display);
            int xb = qt_to_x11(button);
            if (xb != 0) {
                for (int i = 0; i < 2; ++i) {
                    XTestFakeButtonEvent(display, xb, true,  CurrentTime); XFlush(display);
                    usleep(50000);
                    XTestFakeButtonEvent(display, xb, false, CurrentTime); XFlush(display);
                    usleep(50000);
                }
            }
        } else if (type == "wheel") {
            XTestFakeMotionEvent(display, 0, x, y, CurrentTime);
            XFlush(display);
            // X11 滚轮：4=上 5=下 6=左 7=右
            if (button >= 4 && button <= 7) {
                XTestFakeButtonEvent(display, button, true,  CurrentTime); XFlush(display);
                usleep(10000);
                XTestFakeButtonEvent(display, button, false, CurrentTime); XFlush(display);
            }
        }
    };

    // ---------------------------------------------------------------
    // 1015 KEYBOARD_REMOTE_CONTROL —— 远程键盘注入（规范 §3.8A）
    // 客户端 -> 服务端: { "keycode": int, "modifiers": int, "text": string, "type": "pressed"|"released" }
    // ---------------------------------------------------------------
    id_func_map[DataId::KEYBOARD_REMOTE_CONTROL] =
        [this](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;
        if (!body) return;

        Json::Value j;
        Json::CharReaderBuilder rb;
        std::string perr;
        std::string js(body.get(), body.get() + strlen(body.get()));
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(js.data(), js.data() + js.size(), &j, &perr)) return;

        if (!j.isMember("keycode") || !j.isMember("type")) return;

        int qt_key = j["keycode"].asInt();
        std::string type = j["type"].asString();
        bool is_press = (type == "pressed");

        std::lock_guard<std::mutex> lock(display_mutex);
        if (!display) return;

        // Qt Key -> X11 Keysym 映射
        KeySym keysym = NoSymbol;
        if (qt_key >= 0x20 && qt_key <= 0x0ff) {
            // ASCII / Latin-1 常见可打印字符
            if (qt_key >= 'A' && qt_key <= 'Z') {
                keysym = (KeySym)(qt_key + 32); // 默认小写 keysym，修饰键由 Shift 控制
            } else {
                keysym = (KeySym)qt_key;
            }
        } else {
            // 特殊功能键映射
            switch (qt_key) {
                case 0x01000000: keysym = 0xff1b; break; // Escape
                case 0x01000001: keysym = 0xff09; break; // Tab
                case 0x01000003: keysym = 0xff08; break; // BackSpace
                case 0x01000004: case 0x01000005: keysym = 0xff0d; break; // Return / Enter
                case 0x01000006: keysym = 0xff63; break; // Insert
                case 0x01000007: keysym = 0xffff; break; // Delete
                case 0x01000010: keysym = 0xff50; break; // Home
                case 0x01000011: keysym = 0xff57; break; // End
                case 0x01000012: keysym = 0xff51; break; // Left
                case 0x01000013: keysym = 0xff52; break; // Up
                case 0x01000014: keysym = 0xff53; break; // Right
                case 0x01000015: keysym = 0xff54; break; // Down
                case 0x01000016: keysym = 0xff55; break; // PageUp
                case 0x01000017: keysym = 0xff56; break; // PageDown
                case 0x01000020: keysym = 0xffe1; break; // Shift
                case 0x01000021: keysym = 0xffe3; break; // Control
                case 0x01000023: keysym = 0xffe9; break; // Alt
                case 0x01000030: keysym = 0xffbe; break; // F1
                case 0x01000031: keysym = 0xffbf; break; // F2
                case 0x01000032: keysym = 0xffc0; break; // F3
                case 0x01000033: keysym = 0xffc1; break; // F4
                case 0x01000034: keysym = 0xffc2; break; // F5
                case 0x01000035: keysym = 0xffc3; break; // F6
                case 0x01000036: keysym = 0xffc4; break; // F7
                case 0x01000037: keysym = 0xffc5; break; // F8
                case 0x01000038: keysym = 0xffc6; break; // F9
                case 0x01000039: keysym = 0xffc7; break; // F10
                case 0x0100003a: keysym = 0xffc8; break; // F11
                case 0x0100003b: keysym = 0xffc9; break; // F12
                case 0x01000024: keysym = 0xffeb; break; // Super/Win
                default: break;
            }
        }

        if (keysym != NoSymbol) {
            KeyCode keycode = XKeysymToKeycode(display, keysym);
            if (keycode != 0) {
                XTestFakeKeyEvent(display, keycode, is_press ? True : False, CurrentTime);
                XFlush(display);
            }
        }
    };

    // ---------------------------------------------------------------
    // 1010 LOCK_REMOTE_CONTROL —— 远程锁机（规范 §3.9）
    // ---------------------------------------------------------------
    id_func_map[DataId::LOCK_REMOTE_CONTROL] =
        [this](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        // 尝试多种锁屏方式
        int ret = system("loginctl lock-session 2>/dev/null");
        if (ret != 0)
            ret = system("xdg-screensaver lock 2>/dev/null");
        if (ret != 0)
            ret = system("dbus-send --type=method_call --dest=org.gnome.ScreenSaver "
                         "/org/gnome/ScreenSaver org.gnome.ScreenSaver.Lock 2>/dev/null");
        (void)ret; // 客户端不解析响应，可不回
    };

    // ---------------------------------------------------------------
    // 1011 UNLOCK_REMOTE_CONTROL —— 远程解锁（规范 §3.9）
    // ---------------------------------------------------------------
    id_func_map[DataId::UNLOCK_REMOTE_CONTROL] =
        [this](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        int ret = system("loginctl unlock-session 2>/dev/null");
        (void)ret; // 客户端不解析响应，可不回
    };

    // ---------------------------------------------------------------
    // 1012 UPLOAD_FILE —— 文件上传分块接收（规范 §3.4A）
    // 客户端 -> 服务端: { localpath, remotepath, offset, totallen, next, index, length, data:Base64 }
    // 服务端 -> 客户端: { error, recvlen, totallen, finishe, remotepath }
    // ---------------------------------------------------------------
    id_func_map[DataId::UPLOAD_FILE] =
        [](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;
        if (!body) return;

        Json::Value j;
        Json::CharReaderBuilder rb;
        std::string perr;
        std::string js(body.get(), body.get() + strlen(body.get()));
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(js.data(), js.data() + js.size(), &j, &perr)) return;

        if (!j.isMember("remotepath") || !j.isMember("data")) return;

        std::string localpath  = j.isMember("localpath") ? j["localpath"].asString() : "";
        std::string remotepath = j["remotepath"].asString();
        int64_t offset         = j.isMember("offset") ? j["offset"].asInt64() : 0;
        int64_t totallen       = j.isMember("totallen") ? j["totallen"].asInt64() : 0;
        bool next              = j.isMember("next") ? j["next"].asBool() : false;
        int64_t length         = j.isMember("length") ? j["length"].asInt64() : 0;
        std::string data_b64   = j["data"].asString();

        // 家目录白名单校验：remotepath 的父目录必须在家目录内
        size_t last_slash = remotepath.find_last_of('/');
        std::string parent_dir = (last_slash != std::string::npos) ? remotepath.substr(0, last_slash) : "";
        if (parent_dir.empty()) parent_dir = conn->HomeDir();

        std::string safe_parent = conn->SafeRealPath(parent_dir);
        if (safe_parent.empty()) {
            Json::Value resp;
            resp["error"] = 1; // 写入失败
            resp["recvlen"] = (Json::Int64)0;
            resp["totallen"] = (Json::Int64)totallen;
            resp["finishe"] = true;
            resp["remotepath"] = remotepath;
            conn->SendJson(DataId::UPLOAD_FILE, ToJsonStr(resp));
            return;
        }

        // 获取或新建上传任务
        auto task = conn->GetUploadTask(remotepath);
        if (task && task->cancelled) {
            // 已被取消上传，静默丢弃
            return;
        }

        if (!task) {
            task = conn->CreateUploadTask(remotepath, localpath, totallen);
        }

        if (!task || !task->is_open) {
            Json::Value resp;
            resp["error"] = 1; // 打开文件失败
            resp["recvlen"] = (Json::Int64)0;
            resp["totallen"] = (Json::Int64)totallen;
            resp["finishe"] = true;
            resp["remotepath"] = remotepath;
            conn->SendJson(DataId::UPLOAD_FILE, ToJsonStr(resp));
            if (task) conn->RemoveUploadTask(remotepath, true);
            return;
        }

        // 解码 Base64 文件块数据
        std::string chunk_data = base64::decode(data_b64.data(), data_b64.size());
        if (chunk_data.size() != static_cast<size_t>(length)) {
            // 长度不一致
            Json::Value resp;
            resp["error"] = 3; // 接收不完整
            resp["recvlen"] = (Json::Int64)task->recvlen;
            resp["totallen"] = (Json::Int64)totallen;
            resp["finishe"] = true;
            resp["remotepath"] = remotepath;
            conn->SendJson(DataId::UPLOAD_FILE, ToJsonStr(resp));
            conn->RemoveUploadTask(remotepath, true);
            return;
        }

        // 按 offset 定位写入
        task->ofs.seekp(offset, std::ios::beg);
        task->ofs.write(chunk_data.data(), chunk_data.size());
        task->ofs.flush();
        task->recvlen += chunk_data.size();

        bool is_finished = (!next); // next 为 false 表示最后一块
        int err = 0;
        if (is_finished) {
            // 收尾校验
            if (task->recvlen != totallen) {
                err = 3; // 长度不完整
            }
        }

        Json::Value resp;
        resp["error"] = err;
        resp["recvlen"] = (Json::Int64)task->recvlen;
        resp["totallen"] = (Json::Int64)totallen;
        resp["finishe"] = is_finished;
        resp["remotepath"] = remotepath;
        conn->SendJson(DataId::UPLOAD_FILE, ToJsonStr(resp));

        if (is_finished) {
            conn->RemoveUploadTask(remotepath, (err != 0));
        }
    };

    // ---------------------------------------------------------------
    // 1013 CANCEL_UPLOAD_FILE —— 取消文件上传（规范 §3.4B）
    // 客户端 -> 服务端: { localpath, remotepath }
    // ---------------------------------------------------------------
    id_func_map[DataId::CANCEL_UPLOAD_FILE] =
        [](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (conn->GetState() != ConnState::AUTHED) return;
        if (!body) return;

        Json::Value j;
        Json::CharReaderBuilder rb;
        std::string perr;
        std::string js(body.get(), body.get() + strlen(body.get()));
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(js.data(), js.data() + js.size(), &j, &perr)) return;

        if (!j.isMember("remotepath")) return;
        std::string remotepath = j["remotepath"].asString();

        auto task = conn->GetUploadTask(remotepath);
        if (task) {
            task->cancelled = true;
        }
        // 删除半成品并释放资源
        conn->RemoveUploadTask(remotepath, true);
    };

    // ---------------------------------------------------------------
    // 1100 SEND_MSG —— 预留聊天消息（规范 §3.10，S→C 广播）
    // ---------------------------------------------------------------
    id_func_map[DataId::SEND_MSG] =
        [this](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (!body) return;
        std::string json(body.get());
        // 广播给当前连接（此处简化为回声，生产可维护在线连接列表）
        conn->SendJson(DataId::SEND_MSG, json);
    };

    // ---------------------------------------------------------------
    // 1101 SEND_FILE —— 预留聊天文件（规范 §3.10）
    // ---------------------------------------------------------------
    id_func_map[DataId::SEND_FILE] =
        [this](std::shared_ptr<char[]> body, std::shared_ptr<SigConnection> conn)
    {
        if (!body) return;
        std::string json(body.get());
        conn->SendJson(DataId::SEND_FILE, json);
    };

    std::cout << "[SigLogicSystem] registered " << id_func_map.size() << " handlers" << std::endl;
}
