# NoDeskServerKit

> 面向无桌面（Headless）Linux 服务器的高效流媒体点播与远程文件管控系统。

本仓库是 **NoDeskServerKit** 的顶层工作区，包含 **Qt6 客户端** 与 **Linux 服务端** 两个独立子工程，可分别构建、分别部署。

## 项目组成

| 子目录 | 说明 | 构建工具 |
| :--- | :--- | :--- |
| `NoDeskServerKit_Client/` | Windows/Linux 客户端（Qt 6 + libVLC 3.x） | qmake |
| `NoDeskServerKit_LinuxServers/` | Linux 服务端（Reactor TCP + RTSP/RTP） | CMake |

---

## 快速开始

### 1. 构建 Linux 服务端

```bash
cd NoDeskServerKit_LinuxServers
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

产物位于 `NoDeskServerKit_LinuxServers/build/bin/`：

- `NoDeskServerKit` —— 信令 + RTSP 一体化守护进程
- `SigServer` —— 独立信令服务器（默认端口 `10086`）
- `RtspServer` —— 独立流媒体服务器（默认端口 `8554`）
- `SigClientTest` / `RtspClientTest` —— 自动化联调测试客户端

### 2. 构建客户端

```bash
cd NoDeskServerKit_Client
mkdir build_linux && cd build_linux
qmake6 ../NoDeskServerKit_Client.pro CONFIG+=release
make -j$(nproc)
```

> **提示**：若需播放视频，请安装 `libvlc-dev`（Linux）或将 `vlc_sdk` 指向有效 SDK；否则工程会以 `NO_VLC` 模式编译，仅文件/监控功能可用。

### 3. 运行

服务端（非 root 开发模式，使用测试凭证）：

```bash
cd NoDeskServerKit_LinuxServers
SIG_TEST_USER=lzw SIG_TEST_PASS=lzw123 ./build/bin/SigServer 10087 &
./build/bin/RtspServer 8554 &
```

客户端 GUI：

```bash
./NoDeskServerKit_Client/build_linux/NoDeskServerKit_Client
```

登录参数：

- **服务器 IP**：`127.0.0.1`
- **信令端口**：`10087`（与 `SigServer` 一致）
- **RTSP 端口**：`8554`
- **账密**：`lzw` / `lzw123`

---

## 自动化联调

```bash
# 信令服务器全协议自检
./build/bin/SigClientTest 127.0.0.1 10087 lzw lzw123

# RTSP 流媒体自检（需提供本地 MP4 文件）
./build/bin/RtspClientTest 127.0.0.1 8554 /home/lzw/testvideo.mp4
```

---

## 详细文档

- 服务端设计：`NoDeskServerKit_LinuxServers/README.md`、`NoDeskServerKit_LinuxServers/docx/`
- 客户端设计：`NoDeskServerKit_Client/README.md`、`NoDeskServerKit_Client/docx/`
- 实现规范：`NoDeskServerKit_服务端实现规范.md`（见 `docx/`）

## 开源协议

仅供内部研发与科研实验使用。
