cmake_minimum_required(VERSION 3.28)
project(ChargingPlatformServer CXX)

# 强制开启 C++23 标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
    # 1. 直接指定 Boost 解压根目录
    set(BOOST_ROOT "E:/EV-Charging-Platform/boost_1_92_0")

    # 2. 查找线程支持库
    find_package(Threads REQUIRED)

    add_executable(server main.cpp)

    # 3. 引入 Boost 头文件路径与线程库（Header-only，无需 find_package）
    target_include_directories(server PRIVATE
            ${BOOST_ROOT}
    )
    target_link_libraries(server PRIVATE
            Threads::Threads
    )

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