# NoDeskServerKit Linux 服务端 (NoDeskServerKit_LinuxServers)

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/Platform-Linux-orange.svg)](https://www.linux.org/)
[![Network](https://img.shields.io/badge/Reactor-Master--Slave-brightgreen.svg)]()
[![Media](https://img.shields.io/badge/RTSP%2FRTP-H.264%20%7C%20AAC-red.svg)]()
[![Build Status](https://img.shields.io/badge/Build-Passing-success.svg)]()

> **NoDeskServerKit** 是一套专为无桌面 Linux 服务器（Headless Linux Server）打造的高性能、轻量级流媒体低延迟点播与远程文件管控工具套件。
> 本仓库为 **Linux 服务端工程**，提供集**信令控制服务（SigServer）**、**RTSP/RTP 流媒体服务（RtspServer）**以及**开机自启动/守护进程一体化管理（NoDeskServerKit）**于一体的完整后端解决方案。

---

## 目录

- [1. 项目背景与痛点](#1-项目背景与痛点)
- [2. 总体架构与设计](#2-总体架构与设计)
  - [2.1 整体架构拓扑](#21-整体架构拓扑)
  - [2.2 核心模块职责](#22-核心模块职责)
- [3. 核心功能特性](#3-核心功能特性)
  - [3.1 底层网络库 (ReactorTCPNet)](#31-底层网络库-reactortcpnet)
  - [3.2 信令服务器 (SigServer)](#32-信令服务器-sigserver)
  - [3.3 流媒体服务器 (RtspServer)](#33-流媒体服务器-rtspserver)
  - [3.4 远程桌面监控 (方案 B：RTSP 实时推流)](#34-远程桌面监控-方案-b-rtsp-实时推流)
- [4. 通信协议规范](#4-通信协议规范)
  - [4.1 信令二进制帧格式](#41-信令二进制帧格式)
  - [4.2 信令消息 ID 表](#42-信令消息-id-表)
  - [4.3 RTSP 点播与桌面推流交互](#43-rtsp-点播与桌面推流交互)
- [5. 项目目录结构](#5-项目目录结构)
- [6. 依赖环境与构建指南](#6-依赖环境与构建指南)
  - [6.1 系统依赖](#61-系统依赖)
  - [6.2 编译步骤](#62-编译步骤)
- [7. 运行与部署](#7-运行与部署)
  - [7.1 独立服务运行](#71-独立服务运行)
  - [7.2 集成常驻守护进程](#72-集成常驻守护进程)
  - [7.3 Systemd 开机自启服务管理](#73-systemd-开机自启服务管理)
- [8. 自动化测试与联调验证](#8-自动化测试与联调验证)

---

## 1. 项目背景与痛点

在基于深度学习（如 YOLO 目标检测、视频动作识别等）的科研与工程实践中，算法训练服务器通常部署于无图形界面的远程 Ubuntu/CentOS 服务器上。开发者排查训练集抽检与推理效果时存在明显痛点：
1. **多工具割裂切换**：依赖第三方 SSH/SFTP 工具上传测试素材、下载标注推理结果，再通过本地独立播放器打开验证，多端频繁切换，工作流割裂繁琐。
2. **频次高、往返开销极大**：调优验证阶段涉及频繁的视频素材上传、推理结果回传与删除清理，GB 级大体积视频文件的全量下载极度拖慢实验迭代节奏。

**NoDeskServerKit** 将远程文件管控与流媒体低延迟点播融为一体，实现局域网内**远程音视频秒开点播、断点跳转 Seek、512KB 异步分块上传/下载，以及远程桌面与键鼠事件注入**。

---

## 2. 总体架构与设计

### 2.1 整体架构拓扑

```mermaid
graph TD
    subgraph Client ["Windows 客户端 (RemoteControlClient - Qt + libVLC)"]
        UI_Tree["目录与文件树 (DirTreeView / FileTreeView)"]
        UI_VOD["视频播放器 (PlayerDialog + PlayCore)"]
        UI_Watch["远程监控窗 (RemoteWatchWidget)"]
    end

    subgraph Linux_Server ["Linux 服务端 (NoDeskServerKit 集成守护进程)"]
        subgraph NetCore ["ReactorTCPNet 高性能网络基座"]
            EL[EventLoop 多反应堆事件循环]
            ETS[EpollTaskScheduler 子反应堆调度]
            Channel[Channel 事件抽象 + ET 模式]
            Buf[ReadBuf 动态扩容 / WriteBuf 异步队列]
        end

        subgraph SigModule ["信令服务器 SigServer (:10086)"]
            SC[SigConnection : public TcpConnection]
            LS[SigLogicSystem 同步查表分发]
            AUTH[Linux 系统鉴权 getspnam / crypt]
            FILE_IO[分块文件传输 512KB + 家目录白名单校验]
            X11_INJECT[XTest 鼠标 / 键盘事件模拟注入]
        end

        subgraph RtspModule ["流媒体服务器 RtspServer (:8554)"]
            RC[RtspConnection : public TcpConnection]
            SM[RtspSessionManager 会话管理]
            VOD[StreamTask: MP4 VOD 点播解复用]
            DESK[DesktopStreamTask: X11抓屏 + libx264 实时编码]
            RTP[RTP/UDP 打包: RFC 3984 H.264 + RFC 3640 AAC]
        end
    end

    UI_Tree <==>|自定义 TCP 帧 (0xFEFF+JSON)| SC
    UI_Watch -->|鼠标 (1009) / 键盘 (1015)| SC
    UI_VOD <==>|RTSP 信令 (TCP)| RC
    VOD ==>|RTP/UDP 数据流 (视频+音频)| UI_VOD
    UI_Watch <==>|RTSP /desktop 信令 (TCP)| RC
    DESK ==>|实时 H.264 RTP/UDP 流 (30~60ms)| UI_Watch
```

### 2.2 核心模块职责

- **ReactorTCPNet（自研核心网络库）**：主从 Reactor（Multi-Reactor Thread Pool）高并发网络模型。主反应堆负责接收新连接（`Acceptor`），多个子反应堆线程独立负责连接的 I/O 事件循环，利用 Linux epoll ET（边缘触发）模式、非阻塞 I/O 及自动化环形缓冲区保证高并发下零阻塞。
- **SigServer（信令服务器）**：继承自 `TcpServer`，端口 `10086`。统一处理登录鉴权、分区与目录树拉取、大文件分块上传与下载、远程删除与执行、锁屏/解锁、鼠标/键盘注入及桌面环境探测。
- **RtspServer（流媒体服务器）**：继承自 `TcpServer`，端口 `8554`。RTSP 控制信令走 TCP，媒体流走独立 UDP。支持两大路由：
  - `/stream/<filePath>`：MP4 视频文件点播（VOD），支持精确 Seek；
  - `/desktop`：实时远程桌面监控（方案 B），X11 屏幕捕获后通过 `libx264` 极速零延迟编码推流。
- **NoDeskServerKit（集成守护进程入口）**：顶层 `main.cpp` 在多线程中协同启动两大服务器，集成开机自启 systemd 服务的安装、卸载与优雅信号停止机制。

---

## 3. 核心功能特性

### 3.1 底层网络库 (ReactorTCPNet)
- **多 Reactor 主从架构**：由 `EventLoop` 管理 CPU 核心数个 `EpollTaskScheduler`，线程间相互解耦；
- **智能内存缓冲**：`ReadBuf` 支持自适应 2 倍动态扩容与内存紧缩；`WriteBuf` 采用线程安全的数据包队列，配合内核 `EPOLLOUT` 事件驱动异步写，防范大文件挤占与阻塞事件循环；
- **定时器队列**：`TimerEventQueue` 基于最小堆结构维护毫秒级定时事件。

### 3.2 信令服务器 (SigServer)
- **严格二进制帧协议**：`0xFEFF` 帧起始标记扫描，16 位累加和校验防粘包/半包；
- **Linux 系统安全鉴权**：对接 `/etc/shadow` 与 `getspnam()` + `crypt()` 校验系统物理账号；
- **沙箱级家目录白名单**：路径均经过 `realpath` 规范化，强制约束在当前登录用户 `/home/<username>` 范围内，杜绝 `../` 任意文件读取、执行与删除漏洞；
- **512KB 分块流控**：
  - **下载 (`DOWNLOAD_FILE 1003`)**：元数据头帧先行（休眠 200ms 等待客户端创建进度条）→ 512KB 切片持续推送 → 结束帧校验；
  - **上传 (`UPLOAD_FILE 1012`)**：客户端分块推送，服务端按 `offset` 定位写入落盘，逐块回传进度与 `finishe` 状态；
  - **取消上传 (`CANCEL_UPLOAD_FILE 1013`)**：标记取消并丢弃后续数据块，自动清理半成品残损文件；
- **远程键鼠事件注入**：
  - **鼠标 (`1009`)**：归一化浮点坐标换算屏幕绝对像素，支持移动（带客户端节流）、单击、双击、释放及滚轮；
  - **键盘 (`1015`)**：将 Qt KeyCode 映射转换为 Linux X11 KeySym，通过 `XTestFakeKeyEvent` 注入按键按下与释放；
- **无桌面环境自适应**：收到 `JOIN_TCP_REMOTEWATCH (1006)` 时探测 `$DISPLAY` 与 X11 环境，无桌面时返回错误码 `1`（`WATCH_NO_DESKTOP`），防止客户端启动空白窗口。

### 3.3 流媒体服务器 (RtspServer)
- **会话状态机管理**：完整实现 `OPTIONS`、`DESCRIBE`、`SETUP`、`PLAY`、`PAUSE`、`TEARDOWN` 标准 6 种方法；
- **RFC 3984 H.264 封装**：
  - 自动解析 AVCC 长度前缀切分 NALU 单元；
  - 单包 $\le 1300$ 字节打入单个 RTP 包；大于 1300 字节执行 **FU-A 分片封装**；
  - 帧内所有包共用时间戳，时间戳增量严格根据 $90000 / \text{fps}$ 计算；
  - 首包内联 SPS/PPS 确保客户端解码秒开；
- **RFC 3640 AAC 音频封装**：支持 AAC-hbr 模式，按 4 字节 AU-headers 封装裸 AAC 帧，时基对齐采样率；
- **精细流控与时间同步**：采用高精度单调时钟 `sleep_until` 配合帧 pts 差值驱动，杜绝音画不同步与累积漂移；
- **快进快退 Seek 支持**：解析 `Range: npt=xx.xxx-`，通过 FFmpeg `av_seek_frame(BACKWARD)` 寻前置关键帧并执行 `avformat_flush` 重载流上下文。

### 3.4 远程桌面监控 (方案 B：RTSP 实时推流)
- **独立通道解耦**：彻底摒弃方案 A 的 TCP 单张图片轮询方案，视频流转入 RTSP 服务器的独立 `/desktop` 路由（UDP 传输），信令通道只承载控制事件，消除了网络队头阻塞（HOL）；
- **超低延迟实时编码**：采用 `libx264` 编码器配置极速低延迟参数（`preset=ultrafast`, `tune=zerolatency`, `max_b_frames=0`），局域网端到端延迟压至 **30~60ms**。

---

## 4. 通信协议规范

### 4.1 信令二进制帧格式

发送与接收格式严格对称，所有多字节整数均为**网络字节序（Big-Endian）**：

```text
+-------------+----------------+----------------+-------------------+----------------+
| Start Flag  | Data Length    | Message ID     | Message Body      | Checksum       |
| 2 bytes     | 4 bytes        | 2 bytes        | N bytes           | 2 bytes        |
| 0xFE 0xFF   | (BE)           | (BE)           | (JSON, UTF-8)     | (BE)           |
+-------------+----------------+----------------+-------------------+----------------+
```
- **Data Length**：`Message ID (2) + Message Body (N) + Checksum (2)`；
- **Checksum**：`0xFE + 0xFF + DataLen(4字节) + MsgId(2字节) + Body(N字节)` 累加取低 16 位。

### 4.2 信令消息 ID 表

| 消息 ID | 枚举名称 | 方向 | 用途与载荷概要 |
| :--- | :--- | :--- | :--- |
| `1000` | `LOGIN_USER` | 双向 | 远程登录鉴权（输入用户名、密码，返回 `error=0/1/2/3`） |
| `1001` | `GET_DRIVERINFO` | 双向 | 获取本地根路径信息（Linux 单分区返回 `/home/<user>`） |
| `1002` | `GET_DIRINFO` | 双向 | 目录与文件列表逐条推送，以 `next:false` 收尾 |
| `1003` | `DOWNLOAD_FILE` | 双向 | 512KB 分块文件下载（头帧先行 + Base64 切片包） |
| `1004` | `EXECUTE_FILE` | 双向 | 远程执行指定路径程序或脚本 |
| `1005` | `DELETE_FILE` | 双向 | 远程删除指定文件或递归清空目录 |
| `1006` | `JOIN_TCP_REMOTEWATCH` | 双向 | 开启远程监控（探测桌面环境，无桌面返回 `error=1`） |
| `1007` | `GET_REMOTEWATCH_SCREEN` | S→C | 方案 A 兼容预留截屏推送 |
| `1008` | `EXIT_TCP_REMOTEWATCH` | 双向 | 退出远程监控并释放编码与推流资源 |
| `1009` | `MOUSE_REMOTE_CONTROL` | C→S | 鼠标注入（归一化坐标、按键类型、滚轮码） |
| `1010` | `LOCK_REMOTE_CONTROL` | C→S | 远程锁机（执行 `loginctl lock-session`） |
| `1011` | `UNLOCK_REMOTE_CONTROL` | C→S | 远程解锁（执行 `loginctl unlock-session`） |
| `1012` | `UPLOAD_FILE` | 双向 | 512KB 分块文件上传，服务端按 `offset` 落盘并回传进度 |
| `1013` | `CANCEL_UPLOAD_FILE` | C→S | 取消上传，服务端丢弃后续块并删除未完成文件 |
| `1015` | `KEYBOARD_REMOTE_CONTROL`| C→S | 键盘按键注入（Qt keycode 映射 X11 keysym，按下/释放） |
| `1100` | `SEND_MSG` | 双向 | 预留文字聊天消息广播回声 |
| `1101` | `SEND_FILE` | 双向 | 预留聊天文件传输 |

### 4.3 RTSP 点播与桌面推流交互

1. **URL 格式**：
   - 视频点播：`rtsp://<server_ip>:8554/stream/<文件绝对路径>`（兼容双斜杠与 URL 转义）；
   - 桌面实时推流：`rtsp://<server_ip>:8554/desktop`。
2. **方法交互序列**：
   - `OPTIONS` $\to$ 回显支持的公共方法 `Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN`；
   - `DESCRIBE` $\to$ 生成 SDP（H.264 内联 `sprop-parameter-sets`，AAC 内联 `config`）；
   - `SETUP` $\to$ 为视频轨/音频轨分配服务器 UDP RTP/RTCP 端口对，建立 UDP 会话；
   - `PLAY` $\to$ 启动推流线程；解析 `Range: npt=xx.xxx-` 触发 Seek 跳转；
   - `PAUSE` $\to$ 挂起推流工作线程，保留 UDP 通道；
   - `TEARDOWN` $\to$ 停止工作线程，注销会话并回收 UDP 套接字。

---

## 5. 项目目录结构

```text
NoDeskServerKit_LinuxServers/
├── CMakeLists.txt              # 顶层 CMake 构建配置 (生成 ReactorTCPNet, SigServer, RtspServer, NoDeskServerKit)
├── README.md                   # 本说明文档
├── main.cpp                    # 顶层集成服务器守护进程入口 (多线程常驻 + systemd 开机自启服务管理)
├── docx/                       # 设计与规范文档
│   ├── NoDeskServerKit_服务端实现规范.md
│   ├── NoDeskServerKit客户端设计文档.md
│   ├── NoDeskServerKit服务器设计文档.md
│   └── NoServerKit工具.md
├── ReactorTCPNet/              # 自研主从 Reactor C++ 网络库
│   ├── CMakeLists.txt
│   ├── include/                # 网络库头文件 (Acceptor, EventLoop, TcpServer, TcpConnection, ReadBuf, WriteBuf 等)
│   └── src/                    # 网络库源码实现
├── SigServer/                  # 信令服务器模块 (默认端口 10086)
│   ├── CMakeLists.txt
│   ├── include/                # define.h, TcpPacket.h, SigConnection.h, SigLogicSystem.h, SigServer.h
│   ├── src/                    # 帧解析、处理器业务逻辑 (SigHandlers.cpp)、账号鉴权与事件注入
│   └── tests/                  # SigClientTest.cpp (信令协议全功能自动化测试客户端)
└── RtspServer/                 # RTSP/RTP 流媒体点播与实时桌面推流模块 (默认端口 8554)
    ├── CMakeLists.txt
    ├── include/                # RtspCommon.h, RtspRequest.h, RtpPacket.h, StreamTask.h, DesktopStreamTask.h 等
    ├── src/                    # RTSP 解析、SDP 生成、FFmpeg 解复用、x264 编码与 RTP 分包
    └── tests/                  # RtspClientTest.cpp (RTSP/RTP 全链路自动化测试客户端)
```

---

## 6. 依赖环境与构建指南

### 6.1 系统依赖

本工程适用于 **Linux** 环境（推荐 Ubuntu 20.04 / 22.04 LTS 或 Debian 11+），依赖的工具链与开发库如下：

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libjsoncpp-dev \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libswscale-dev \
    libx264-dev \
    libx11-dev \
    libxtst-dev
```

### 6.2 编译步骤

在项目根目录下通过 CMake 完成编译构建：

```bash
# 1. 创建并进入构建目录
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. 并行编译所有目标
cmake --build build -j$(nproc)
```

构建成功后，在 `build/bin/` 目录下生成以下二进制目标：
- `NoDeskServerKit`：信令 + RTSP 协同守护进程集成程序；
- `SigServer`：独立的信令服务器可执行程序；
- `RtspServer`：独立的 RTSP 流媒体服务器可执行程序；
- `SigClientTest`：信令协议全量自动化测试客户端；
- `RtspClientTest`：RTSP 流媒体点播与桌面推流测试客户端。

---

## 7. 运行与部署

### 7.1 独立服务运行

如果需要将信令服务器与流媒体服务器分别部署在不同容器、进程或主机上，可单独启动：

```bash
# 启动独立信令服务器 (默认 10086 端口，可通过参数修改)
./build/bin/SigServer 10086

# 启动独立 RTSP 服务器 (默认 8554 端口，可通过参数修改)
./build/bin/RtspServer 8554
```

### 7.2 集成常驻守护进程

运行顶层集成程序 `NoDeskServerKit`，程序将同时拉起信令服务器与 RTSP 服务器，并在主线程进行统一常驻守护：

```bash
./build/bin/NoDeskServerKit
```

控制台输出示例：
```text
========================================================
      NoDeskServerKit 集成服务器 (信令 + RTSP 流媒体)     
========================================================
[NoDeskServerKit] 启动信令服务器 (端口: 10086)...
[NoDeskServerKit] 启动RTSP服务器 (端口: 8554)...
[NoDeskServerKit] 服务启动完毕，进入守护运行模式。
listening(0.0.0.0:10086)........
listening(0.0.0.0:8554)........
```

按 `Ctrl + C` 或向进程发送 `SIGTERM` 信号，服务端将安全注销会话、关闭 Reactor 事件循环并优雅退出。

### 7.3 Systemd 开机自启服务管理

集成程序原生支持注册为标准的 Linux `systemd` 系统服务，实现开机自动拉起、异常崩溃自动重启：

```bash
# 以 root 权限运行一次程序，将自动创建 /etc/systemd/system/RemoteControlServer.service 并启用自启
sudo ./build/bin/NoDeskServerKit

# 服务注册成功后，可通过标准 systemctl 命令管理
sudo systemctl status RemoteControlServer
sudo systemctl restart RemoteControlServer
sudo systemctl stop RemoteControlServer

# 卸载开机自启服务并清理配置文件
sudo ./build/bin/NoDeskServerKit --uninstall
```

---

## 8. 自动化测试与联调验证

工程自带针对各业务协议的全自动化黑盒测试程序，无需依赖客户端 UI 即可完成服务器端各功能分支的完整性自检。

### 8.1 信令服务器自动化测试 (`SigClientTest`)

`SigClientTest` 覆盖全部信令交互，包含登录鉴权、目录拉取、大文件分块下载、大文件分块上传、落盘校验、取消上传、鼠标与键盘事件注入等：

```bash
# 后台启动测试端口服务器 (开发环境支持通过环境变量传入测试账号)
SIG_TEST_USER=lzw SIG_TEST_PASS=lzw123 ./build/bin/SigServer 10087 &

# 运行自动化测试
./build/bin/SigClientTest 127.0.0.1 10087 lzw lzw123
```

测试执行结果：
```text
=== SigClientTest -> 127.0.0.1:10087 ===

[1000 LOGIN_USER]
  [PASS] 登录响应 error=0
[1001 GET_DRIVERINFO]
  [PASS] error=0
  [PASS] system=Linux
  [PASS] driverinfo 非空
[1002 GET_DIRINFO]
  [PASS] 收到 1002 目录条目
  [PASS] 包含 next:false 结束帧
[1004 EXECUTE_FILE]
  [PASS] 收到 1004 响应
[1005 DELETE_FILE]
  [PASS] 收到 1005 响应
[1006 JOIN / 1007 SCREEN / 1008 EXIT 远程监控组]
  [PASS] 加入监控 error=0
  [PASS] 收到 1007 截图推送
  [PASS] 退出监控 error=0
[1009 MOUSE_REMOTE_CONTROL]
  [PASS] 发送鼠标事件（无响应消息）
  [PASS] 鼠标注入后连接存活
[1015 KEYBOARD_REMOTE_CONTROL]
  [PASS] 发送键盘按下/释放事件（无响应消息）
  [PASS] 键盘注入后连接存活
[1010 LOCK / 1011 UNLOCK]
  [PASS] 发送锁机
  [PASS] 发送解锁
  [PASS] 锁/解锁后连接存活
[1100 SEND_MSG / 1101 SEND_FILE]
  [PASS] 1100 聊天消息回声
  [PASS] 1101 聊天文件回声
[1003 DOWNLOAD_FILE]
  [PASS] 不存在文件下载返回 error=1 head=true
  [PASS] 真实文件下载头帧 error=0 head=true
  [PASS] 收到下载分块与结束帧
[1012 UPLOAD_FILE / 1013 CANCEL_UPLOAD_FILE]
  [PASS] 文件上传响应 error=0 finishe=true
  [PASS] 上传文件内容与原文一致
  [PASS] 取消上传后连接存活

========================================
全部测试结果: PASS=26 FAIL=0
========================================
```

### 8.2 RTSP 流媒体服务器自动化测试 (`RtspClientTest`)

`RtspClientTest` 校验完整的 RTSP 握手、SDP 解析、UDP 端口协商、点播推流、Seek 跳转、连接关闭，以及方案 B 实时桌面推流路由：

```bash
# 启动 RTSP 服务器
./build/bin/RtspServer 8554 &

# 运行自动化测试 (提供本地真实 MP4 文件路径)
./build/bin/RtspClientTest 127.0.0.1 8554 /path/to/test.mp4
```

测试执行结果：
```text
=== RtspClientTest -> 127.0.0.1:8554 ===

[1. OPTIONS]
  [PASS] OPTIONS 响应包含 200 OK
  [PASS] 包含 Public 方法支持
[2. DESCRIBE]
  [PASS] DESCRIBE 响应包含 200 OK
  [PASS] SDP 包含 H264 视频轨道
[3. SETUP]
  [PASS] SETUP 响应包含 200 OK
  [PASS] SETUP 返回 Session
[4. PLAY]
  [PASS] PLAY 响应包含 200 OK
  [PASS] 收到服务端 UDP 推送的 H.264 RTP 数据包
[5. PAUSE]
  [PASS] PAUSE 响应包含 200 OK
[6. PLAY Seek]
  [PASS] PLAY Seek 响应包含 200 OK
[7. TEARDOWN]
  [PASS] TEARDOWN 响应包含 200 OK
[8. 方案 B 实时桌面推流 (/desktop)]
  [PASS] 桌面推流 DESCRIBE 响应 200 OK 且包含视频轨
  [PASS] 桌面推流 SETUP 响应 200 OK
  [PASS] 桌面推流 PLAY 响应 200 OK
  [PASS] 桌面推流 TEARDOWN 响应 200 OK

========================================
RtspServer 测试结果: PASS=15 FAIL=0
========================================
```

---

## 开源协议与声明

本项目遵循内部研发使用规范，仅用于科研排查与工程实验效率提升场景。未经授权请勿直接用于涉及外部公网未授权渗透等未定义场景。
