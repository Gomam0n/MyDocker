cmake_minimum_required(VERSION 3.10)

# 项目名称和版本
project(MyDocker VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# 包含头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 查找所需的库
find_package(Threads REQUIRED)

# 源文件
set(SOURCES
    simpleDocker.cpp
    common/utils.cpp
    logging/logging.cpp
    network/network.cpp
    container/container.cpp
    filesystem/filesystem.cpp
    cgroup/cgroup.cpp
)
# 头文件
set(HEADERS
    common/constants.h
    common/structures.h
    common/utils.h
    logging/logging.h
    network/network.h
    container/container.h
    filesystem/filesystem.h
    cgroup/cgroup.h
)

# 创建可执行文件
add_executable(simple ${SOURCES} ${HEADERS})

# 链接库
target_link_libraries(simple
    Threads::Threads
)

# 设置输出目录
set_target_properties(simple PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 安装规则
install(TARGETS simple
    RUNTIME DESTINATION bin
)

# 打印构建信息
message(STATUS "MyDocker项目配置完成")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "编译器: ${CMAKE_CXX_COMPILER_ID}")