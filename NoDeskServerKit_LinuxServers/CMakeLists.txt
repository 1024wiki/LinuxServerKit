cmake_minimum_required(VERSION 3.20.0)
project(NoDeskServerKit)

# 设置子目录项目通用属性
set(CMAKE_CXX_STANDARD 17)

# 设置网络库的头文件路径TCPNET由子CMakeLists.txt继承使用
set(TCPNET ${PROJECT_SOURCE_DIR}/ReactorTCPNet/include)

# 设置可执行文件、动态库、静态库的目标存放路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# 添加子目录(按照添加子目录顺序进行CMakeLists.txt的构建完后进行返回)
add_subdirectory(${PROJECT_SOURCE_DIR}/ReactorTCPNet)
add_subdirectory(${PROJECT_SOURCE_DIR}/SigServer)
add_subdirectory(${PROJECT_SOURCE_DIR}/RtspServer)

# ========================================================
# 顶层集成可执行程序 NoDeskServerKit (同时集成 SigServer 与 RtspServer)
# ========================================================
include_directories(
    ${TCPNET}
    ${PROJECT_SOURCE_DIR}/SigServer/include
    ${PROJECT_SOURCE_DIR}/RtspServer/include
)

# 收集 SigServer 的所有实现源码 (排除原来的独立 main.cpp)
file(GLOB SIG_SOURCES
    ${PROJECT_SOURCE_DIR}/SigServer/src/*.cpp
)
list(FILTER SIG_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# 收集 RtspServer 的所有实现源码 (排除原来的独立 main.cpp)
file(GLOB RTSP_SOURCES
    ${PROJECT_SOURCE_DIR}/RtspServer/src/*.cpp
)
list(FILTER RTSP_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_executable(NoDeskServerKit
    ${PROJECT_SOURCE_DIR}/main.cpp
    ${SIG_SOURCES}
    ${RTSP_SOURCES}
)

# 链接 ReactorTCPNet 静态库及所有上层功能依赖:
#   - crypt : Linux 登录鉴权 (shadow/getspnam)
#   - X11, Xtst : 远程监控截屏与鼠标模拟注入
#   - jsoncpp : JSON 消息解析与组包
#   - avformat, avcodec, avutil, swscale : FFmpeg 视频点播与实时桌面 H.264 编码
#   - pthread : 多线程支持
target_link_libraries(NoDeskServerKit
    ReactorTCPNet
    crypt
    X11
    Xtst
    jsoncpp
    avformat
    avcodec
    avutil
    swscale
    pthread
)