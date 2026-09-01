cmake_minimum_required(VERSION 3.28)
project(ChargingPlatformServer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找线程库
find_package(Threads REQUIRED)

add_executable(server main.cpp)

# 通用依赖链接
target_link_libraries(server PRIVATE Threads::Threads)

if (WIN32)
    # Windows 环境配置
    set(BOOST_ROOT "E:/EV-Charging-Platform/boost_1_92_0" CACHE PATH "Boost root path")
    target_include_directories(server PRIVATE ${BOOST_ROOT})
    target_compile_definitions(server PRIVATE
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(server PRIVATE
        ws2_32
        mswsock
    )
else()
    # Ubuntu / Linux 环境配置
    find_package(Boost REQUIRED)
    
    # Beast 与 Asio 基础功能为 Header-only
    if (TARGET Boost::headers)
        target_link_libraries(server PRIVATE Boost::headers)
    else()
        target_include_directories(server PRIVATE ${Boost_INCLUDE_DIRS})
    endif()
endif()