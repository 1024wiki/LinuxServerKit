project(SigServer)

# 添加头文件路径，TCPNET由父CMakeLists.txt继承的网络库头文件路径
include_directories(${PROJECT_SOURCE_DIR}/include ${TCPNET})
aux_source_directory(${PROJECT_SOURCE_DIR}/src sources)

# add_compile_definitions(SIGDEBUG)
message(STATUS "找到的源文件: ${sources}") 

add_executable(SigServer ${sources})

# 链接网络库ReactorTCPNet.a，以及鉴权/输入注入/多线程/JSON依赖
#   crypt : getspnam+crypt 登录鉴权(规范§3.1)
#   X11/Xtst : XTest 鼠标注入与截屏(规范§3.7/§3.8)
#   jsoncpp : 信令载荷 JSON 解析与构建(#include "jsoncpp/json/json.h")
target_link_libraries(SigServer ReactorTCPNet crypt X11 Xtst jsoncpp pthread)

# 测试客户端：拉流控制端和推流被控制端（源文件存在时才构建）
if(EXISTS ${PROJECT_SOURCE_DIR}/tests/SigClients.cpp)
    add_executable(SigClients tests/SigClients.cpp)
endif()

# 协议测试客户端：覆盖全部 DataId 消息（无外部依赖，纯 POSIX socket）
if(EXISTS ${PROJECT_SOURCE_DIR}/tests/SigClientTest.cpp)
    add_executable(SigClientTest tests/SigClientTest.cpp)
endif()
