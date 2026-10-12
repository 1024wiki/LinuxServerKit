project(ReactorTCPNet)  # 执行这段CMAKE指令会更新PROJECT_SOURCE_DIR为当前CMakeLists.txt的同级目录下

# 头文件所在目录
include_directories(${PROJECT_SOURCE_DIR}/include)
# 获取所有源文件
aux_source_directory(${PROJECT_SOURCE_DIR}/src sources)
message(STATUS "找到的源文件: ${sources}")  

# 设置编译方式并设置DEBUG宏
# set(CMAKE_BUILD_TYPE Debug)
add_compile_definitions(RELEASE) # RELEASE/DEBUG

# 编译生成静态库文件
add_library(ReactorTCPNet STATIC ${sources})
add_executable(TcpNet ${sources})