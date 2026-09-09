cmake_minimum_required(VERSION 3.22)
project(ChargingPlatformServer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# 查找线程库
find_package(Threads REQUIRED)

# 查找 Qt (优先 Qt 6，自动兼容 Qt 5)
if (NOT CMAKE_PREFIX_PATH AND EXISTS "E:/Qt/6.11.0/msvc2022_64")
    set(CMAKE_PREFIX_PATH "E:/Qt/6.11.0/msvc2022_64")
endif()

find_package(Qt6 COMPONENTS Core Network QUIET)
if (Qt6_FOUND)
    message(STATUS "Found Qt6: Core, Network (version ${Qt6_VERSION})")
    set(QT_LIBRARIES Qt6::Core Qt6::Network)
else()
    message(STATUS "Qt6 not found, checking for Qt5 fallback...")
    find_package(Qt5 REQUIRED COMPONENTS Core Network)
    message(STATUS "Found Qt5: Core, Network (version ${Qt5_VERSION})")
    set(QT_LIBRARIES Qt5::Core Qt5::Network)
endif()

# ===================================================
# 压测模式控制参数 (默认 OFF: 保证正常版本完全不变)
# ===================================================
option(ENABLE_BENCHMARK "Build benchmark tools and enable large-scale dataset seed" OFF)

if (ENABLE_BENCHMARK)
    message(STATUS ">>> [BENCHMARK MODE ENABLED] Large-scale dataset and stress_client will be built.")
    add_compile_definitions(BENCHMARK_MODE=1)
endif()

set(SERVER_SOURCES
    main.cpp
    cache/redis_cache.cpp
    db/db_pool.cpp
    db/db_repository.cpp
    db/seed_data.cpp
    router/http_router.cpp
    websocket/ws_session.cpp
    websocket/qt_ws_session.cpp
    server/qt_http_server.cpp
    server/qt_http_session.cpp
    simulation/simulator.cpp
)

add_executable(server ${SERVER_SOURCES})

# ===================================================
# 1. 查找 Header-only 库: Boost 与 Glaze
# ===================================================

set(BOOST_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/../boost_1_92_0"
    "${CMAKE_SOURCE_DIR}/boost_1_92_0"
    "${CMAKE_CURRENT_SOURCE_DIR}/boost_1_92_0"
)

set(BOOST_INCLUDE_FOUND FALSE)
set(LOCAL_BOOST_PATH "")
foreach(PATH ${BOOST_SEARCH_PATHS})
    if(EXISTS "${PATH}/boost/version.hpp")
        set(LOCAL_BOOST_PATH "${PATH}")
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

set(GLAZE_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/../glaze-8.3.0/include"
    "${CMAKE_SOURCE_DIR}/glaze-8.3.0/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/glaze-8.3.0/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../glaze/include"
    "${CMAKE_SOURCE_DIR}/glaze/include"
)

set(GLAZE_INCLUDE_FOUND FALSE)
set(LOCAL_GLAZE_PATH "")
foreach(PATH ${GLAZE_SEARCH_PATHS})
    if(EXISTS "${PATH}/glaze/glaze.hpp")
        set(LOCAL_GLAZE_PATH "${PATH}")
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
        set(LOCAL_GLAZE_PATH "${GLAZE_INCLUDE_DIR}")
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

    target_compile_definitions(server PRIVATE
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    set(PG_WIN_ROOT "C:/Program Files/PostgreSQL/18" CACHE PATH "PostgreSQL root directory on Windows")
    target_include_directories(server PRIVATE "${PG_WIN_ROOT}/include")

    if (MSVC)
        target_compile_options(server PRIVATE
            /std:c++latest
            /utf-8
            /bigobj
            /EHsc
            /Zc:__cplusplus
            /O2
            /fp:fast
            /arch:AVX2
        )

        target_link_libraries(server PRIVATE
            Threads::Threads
            ${QT_LIBRARIES}
            "${PG_WIN_ROOT}/lib/libpq.lib"
            ws2_32
            mswsock
        )
    else()
        if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(server PRIVATE
                -Wa,-mbig-obj
                -O3
                -mavx2
                -mfma
                -ffast-math
            )
        endif()

        target_link_libraries(server PRIVATE
            Threads::Threads
            ${QT_LIBRARIES}
            "${PG_WIN_ROOT}/bin/libpq.dll"
            "${PG_WIN_ROOT}/bin/libcrypto-3-x64.dll"
            "${PG_WIN_ROOT}/bin/libssl-3-x64.dll"
            ws2_32
            mswsock
            stdc++exp
        )
    endif()

else()
    message(STATUS "Configuring for Linux / Unix platform...")

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(server PRIVATE
            -O3
            -mavx2
            -mfma
            -ffast-math
            -pthread
        )
    else()
        target_compile_options(server PRIVATE
            -O3
            -pthread
        )
    endif()

    find_package(PostgreSQL REQUIRED)
    if (TARGET PostgreSQL::PostgreSQL)
        target_link_libraries(server PRIVATE PostgreSQL::PostgreSQL)
    else()
        target_include_directories(server PRIVATE ${PostgreSQL_INCLUDE_DIRS})
        target_link_libraries(server PRIVATE ${PostgreSQL_LIBRARIES})
    endif()

    target_link_libraries(server PRIVATE
        Threads::Threads
        ${QT_LIBRARIES}
    )

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        find_library(STDCXXEXP_LIB stdc++exp)
        if (STDCXXEXP_LIB)
            target_link_libraries(server PRIVATE stdc++exp)
        endif()
    endif()

endif()

# ===================================================
# 3. 压测客户端可执行文件 (仅在 ENABLE_BENCHMARK=ON 时构建)
# ===================================================

if (ENABLE_BENCHMARK)
    add_executable(stress_client benchmark/stress_client.cpp)

    if (LOCAL_BOOST_PATH)
        target_include_directories(stress_client PRIVATE "${LOCAL_BOOST_PATH}")
    endif()
    if (LOCAL_GLAZE_PATH)
        target_include_directories(stress_client PRIVATE "${LOCAL_GLAZE_PATH}")
    endif()

    target_include_directories(stress_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    if (WIN32)
        target_compile_definitions(stress_client PRIVATE
            _WIN32_WINNT=0x0A00
            WIN32_LEAN_AND_MEAN
            NOMINMAX
        )
        if (MSVC)
            target_compile_options(stress_client PRIVATE
                /std:c++latest
                /utf-8
                /bigobj
                /EHsc
                /Zc:__cplusplus
                /O2
                /fp:fast
                /arch:AVX2
            )
            target_link_libraries(stress_client PRIVATE
                Threads::Threads
                ws2_32
                mswsock
                psapi
            )
        else()
            if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(stress_client PRIVATE
                    -Wa,-mbig-obj
                    -O3
                    -mavx2
                    -mfma
                    -ffast-math
                )
            endif()
            target_link_libraries(stress_client PRIVATE
                Threads::Threads
                ws2_32
                mswsock
                stdc++exp
                psapi
            )
        endif()
    else()
        if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(stress_client PRIVATE
                -O3
                -mavx2
                -mfma
                -ffast-math
                -pthread
            )
        else()
            target_compile_options(stress_client PRIVATE
                -O3
                -pthread
            )
        endif()
        target_link_libraries(stress_client PRIVATE
            Threads::Threads
        )
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            find_library(STDCXXEXP_LIB stdc++exp)
            if (STDCXXEXP_LIB)
                target_link_libraries(stress_client PRIVATE stdc++exp)
            endif()
        endif()
    endif()
endif()