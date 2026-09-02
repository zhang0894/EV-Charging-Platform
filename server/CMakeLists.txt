cmake_minimum_required(VERSION 3.28)
project(ChargingPlatformServer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找线程库
find_package(Threads REQUIRED)

set(SERVER_SOURCES
    main.cpp
    db/db_pool.cpp
    db/db_repository.cpp
    db/seed_data.cpp
    router/http_router.cpp
    websocket/ws_session.cpp
    simulation/simulator.cpp
)

add_executable(server ${SERVER_SOURCES})

# ===================================================
# 1. 查找 Header-only 库: Boost 与 Glaze
# ===================================================

# 探测项目根目录或上一级目录中的 Boost 文件夹
set(BOOST_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/../boost_1_92_0"
    "${CMAKE_SOURCE_DIR}/boost_1_92_0"
    "${CMAKE_CURRENT_SOURCE_DIR}/boost_1_92_0"
)

set(BOOST_INCLUDE_FOUND FALSE)
foreach(PATH ${BOOST_SEARCH_PATHS})
    if(EXISTS "${PATH}/boost/version.hpp")
        target_include_directories(server PRIVATE "${PATH}")
        set(BOOST_INCLUDE_FOUND TRUE)
        message(STATUS "Found local Boost headers at: ${PATH}")
        break()
    endif()
endforeach()

if(NOT BOOST_INCLUDE_FOUND)
    message(STATUS "Local Boost folder not found, searching system Boost...")
    find_package(Boost REQUIRED)
    if(TARGET Boost::headers)
        target_link_libraries(server PRIVATE Boost::headers)
    else()
        target_include_directories(server PRIVATE ${Boost_INCLUDE_DIRS})
    endif()
endif()

# 探测项目根目录或上一级目录中的 Glaze 文件夹
set(GLAZE_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/../glaze-8.3.0/include"
    "${CMAKE_SOURCE_DIR}/glaze-8.3.0/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/glaze-8.3.0/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../glaze/include"
    "${CMAKE_SOURCE_DIR}/glaze/include"
)

set(GLAZE_INCLUDE_FOUND FALSE)
foreach(PATH ${GLAZE_SEARCH_PATHS})
    if(EXISTS "${PATH}/glaze/glaze.hpp")
        target_include_directories(server PRIVATE "${PATH}")
        set(GLAZE_INCLUDE_FOUND TRUE)
        message(STATUS "Found local Glaze headers at: ${PATH}")
        break()
    endif()
endforeach()

if(NOT GLAZE_INCLUDE_FOUND)
    message(STATUS "Local Glaze folder not found, searching system include paths...")
    find_path(GLAZE_INCLUDE_DIR glaze/glaze.hpp)
    if(GLAZE_INCLUDE_DIR)
        target_include_directories(server PRIVATE ${GLAZE_INCLUDE_DIR})
    else()
        message(WARNING "Glaze headers not found automatically. Please ensure glaze header folder is in project root.")
    endif()
endif()

target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# ===================================================
# 2. 平台差异化配置 (Windows vs Linux)
# ===================================================

if (WIN32)
    message(STATUS "Configuring for Windows platform (MinGW / MSVC)...")

    # Windows 宏与编译选项
    target_compile_definitions(server PRIVATE
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(server PRIVATE
            -Wa,-mbig-obj
            -O2
        )
    endif()

    # Windows PostgreSQL 默认路径
    set(PG_WIN_ROOT "C:/Program Files/PostgreSQL/18" CACHE PATH "PostgreSQL root directory on Windows")
    target_include_directories(server PRIVATE "${PG_WIN_ROOT}/include")

    target_link_libraries(server PRIVATE
        Threads::Threads
        "${PG_WIN_ROOT}/bin/libpq.dll"
        "${PG_WIN_ROOT}/bin/libcrypto-3-x64.dll"
        "${PG_WIN_ROOT}/bin/libssl-3-x64.dll"
        ws2_32
        mswsock
        stdc++exp
    )

else()
    message(STATUS "Configuring for Linux / Unix platform...")

    # Linux 编译优化与多线程
    target_compile_options(server PRIVATE
        -O2
        -pthread
    )

    # 查找 Linux 下通过 apt 安装的 PostgreSQL (libpq-dev)
    find_package(PostgreSQL REQUIRED)
    if (TARGET PostgreSQL::PostgreSQL)
        target_link_libraries(server PRIVATE PostgreSQL::PostgreSQL)
    else()
        target_include_directories(server PRIVATE ${PostgreSQL_INCLUDE_DIRS})
        target_link_libraries(server PRIVATE ${PostgreSQL_LIBRARIES})
    endif()

    # 查找 Linux 下的 OpenSSL 库 (用于 SSL/Crypto)
    find_package(OpenSSL REQUIRED)
    if (TARGET OpenSSL::SSL)
        target_link_libraries(server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    else()
        target_include_directories(server PRIVATE ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(server PRIVATE ${OPENSSL_LIBRARIES})
    endif()

    # 基础链接库
    target_link_libraries(server PRIVATE
        Threads::Threads
    )

    # GCC 13/14/15/16 下 C++23 std::print / std::expected 扩展库支持
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        include(CheckLibraryExists)
        check_library_exists(stdc++exp _ZSt7printlnIJPKcEEvSt13basic_ostreamIcSt11char_traitsIcEEDpOT_ "" HAVE_LIBSTDCXXEXP)
        # 尝试链接 stdc++exp (若系统 libstdc++ 需要)
        find_library(STDCXXEXP_LIB stdc++exp)
        if (STDCXXEXP_LIB)
            target_link_libraries(server PRIVATE stdc++exp)
        endif()
    endif()

endif()