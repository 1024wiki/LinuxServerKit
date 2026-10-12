project(RtspServer)

include_directories(${TCPNET} ${PROJECT_SOURCE_DIR}/include)

aux_source_directory(${PROJECT_SOURCE_DIR}/src sources)

message(STATUS "RtspServer 源文件: ${sources}")
add_compile_definitions(RTSPDEBUG)

add_executable(RtspServer ${sources})

# 链接 ReactorTCPNet 静态库及 FFmpeg 与 X11 依赖
target_link_libraries(RtspServer
    ReactorTCPNet
    avformat
    avcodec
    avutil
    swscale
    X11
    pthread
)

# RTSP 协议全链路测试客户端
if(EXISTS ${PROJECT_SOURCE_DIR}/tests/RtspClientTest.cpp)
    add_executable(RtspClientTest tests/RtspClientTest.cpp)
endif()

