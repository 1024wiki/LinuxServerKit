#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <climits>
#include <cstdlib>

// 信令服务器与 RTSP 流媒体服务器头文件
#include "SigServer.h"
#include "SigLogicSystem.h"
#include "RtspServer.h"

// 运行标志
static std::atomic<bool> g_running{true};

static void SignalHandler(int signum) {
    std::cout << "\n[NoDeskServerKit] 接收到终止信号 (" << signum << ")，正在优雅停止服务..." << std::endl;
    g_running.store(false);
}

// 检查是否具备 root 权限
static bool isRunningAsRoot() {
    return (geteuid() == 0);
}

// 获取当前可执行程序的绝对路径
static std::string getSelfExecutablePath() {
    char buf[PATH_MAX] = {0};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return "";
}

// 检查文件是否存在
static bool fileExists(const std::string& path) {
    struct stat st;
    return (stat(path.c_str(), &st) == 0);
}

// 为系统安装本程序为 systemd 服务，并设置开机自启动
static bool installAsService(const std::string& appname) {
    std::string execPath = getSelfExecutablePath();
    if (execPath.empty()) {
        std::cerr << "[Service] 无法获取当前可执行文件路径！" << std::endl;
        return false;
    }

    std::string serviceFilePath = "/etc/systemd/system/" + appname + ".service";

    // 生成 systemd unit 配置文件内容
    std::ofstream ofs(serviceFilePath);
    if (!ofs.is_open()) {
        std::cerr << "[Service] 无法创建服务配置文件: " << serviceFilePath << std::endl;
        return false;
    }

    ofs << "[Unit]\n"
        << "Description=NoDeskServerKit Signaling and RTSP Streaming Server\n"
        << "After=network.target\n\n"
        << "[Service]\n"
        << "Type=simple\n"
        << "ExecStart=" << execPath << "\n"
        << "Restart=always\n"
        << "RestartSec=3\n"
        << "KillMode=process\n\n"
        << "[Install]\n"
        << "WantedBy=multi-user.target\n";
    ofs.close();

    // 重新加载 systemd daemon 并启用开机自启
    int ret = system("systemctl daemon-reload >/dev/null 2>&1");
    if (ret != 0) return false;

    std::string enableCmd = "systemctl enable " + appname + " >/dev/null 2>&1";
    ret = system(enableCmd.c_str());
    return (ret == 0);
}

// 卸载本程序的 systemd 服务
static void uninstallAsService(const std::string& appname) {
    std::string serviceFilePath = "/etc/systemd/system/" + appname + ".service";

    std::string stopCmd = "systemctl stop " + appname + " >/dev/null 2>&1";
    system(stopCmd.c_str());

    std::string disableCmd = "systemctl disable " + appname + " >/dev/null 2>&1";
    system(disableCmd.c_str());

    if (unlink(serviceFilePath.c_str()) == 0) {
        std::cout << "[Service] 已成功删除服务文件: " << serviceFilePath << std::endl;
    } else {
        std::cerr << "[Service] 删除服务文件失败: " << serviceFilePath << std::endl;
    }

    system("systemctl daemon-reload >/dev/null 2>&1");
    std::cout << "[Service] 卸载服务 " << appname << " 完成！" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "========================================================" << std::endl;
    std::cout << "      NoDeskServerKit 集成服务器 (信令 + RTSP 流媒体)     " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::string appname = "NoDeskServerKit";
    std::string appServerPath = "/etc/systemd/system/" + appname + ".service";

    // 检查命令行参数
    bool uninstall_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--uninstall") {
            uninstall_mode = true;
            break;
        }
    }

    if (!uninstall_mode) {
        // 不需要卸载时，检查是否已经安装该程序为系统服务，未安装则尝试安装
        if (fileExists(appServerPath)) {
            std::cout << "该服务程序已经存在！" << std::endl;
        } else {
            // 判断获取 root 权限，没有 root 权限无法实现开机自启动
            if (!isRunningAsRoot()) {
                std::cout << "没有获取root权限启动程序无法进行安装。" << std::endl;
            } else {
                if (installAsService(appname)) {
                    std::cout << "创建本程序为服务程序，并设置为开机自启动方式成功。" << std::endl;
                } else {
                    std::cout << "创建本程序为服务程序，并设置为开机自启动方式失败。" << std::endl;
                }
            }
        }
    } else {
        // 卸载模式
        if (fileExists(appServerPath)) {
            if (!isRunningAsRoot()) {
                std::cout << "没有获取root权限启动程序无法进行卸载本服务程序。" << std::endl;
            } else {
                uninstallAsService(appname);
            }
        } else {
            std::cout << "服务程序未安装，无需卸载。" << std::endl;
        }
        return 0; // 卸载完毕后退出
    }

    // 正常启动模式检查权限
    if (!isRunningAsRoot()) {
        std::cout << "没有获取到root权限，将以普通用户权限启动本程序，部分功能可能受限！" << std::endl;
    }

    // 注册信号处理，以便优雅关闭
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // 默认端口
    uint16_t sig_port  = 10086;
    uint16_t rtsp_port = 8554;

    std::cout << "[NoDeskServerKit] 启动信令服务器 (端口: " << sig_port << ")..." << std::endl;
    std::cout << "[NoDeskServerKit] 启动RTSP服务器 (端口: " << rtsp_port << ")..." << std::endl;

    // 获取并启动信令服务器与 RTSP 服务器单例
    auto sig_server  = SigServer::GetInstances(0, "0.0.0.0", sig_port);
    auto rtsp_server = RtspServer::GetInstances(0, "0.0.0.0", rtsp_port);

    // 在独立后台线程中启动信令服务器 Reactor 事件循环
    std::thread sig_thread([sig_server]() {
        sig_server->Start(TcpServer::ReactorModel::MASTER_SLAVE);
    });

    // 在独立后台线程中启动 RTSP 服务器 Reactor 事件循环
    std::thread rtsp_thread([rtsp_server]() {
        rtsp_server->Start(TcpServer::ReactorModel::MASTER_SLAVE);
    });

    std::cout << "[NoDeskServerKit] 服务启动完毕，进入守护运行模式。" << std::endl;

    // 守护主线程等待退出信号
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // 优雅关闭服务
    std::cout << "[NoDeskServerKit] 正在关闭信令服务器与 RTSP 服务器..." << std::endl;
    SigLogicSystem::GetInstances()->Stop();
    sig_server->Close();
    rtsp_server->Close();

    if (sig_thread.joinable())  sig_thread.join();
    if (rtsp_thread.joinable()) rtsp_thread.join();

    std::cout << "[NoDeskServerKit] 所有服务已安全退出。" << std::endl;
    return 0;
}
