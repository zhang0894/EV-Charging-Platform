cmake_minimum_required(VERSION 3.28)
project(ChargingPlatformServer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找线程库
find_package(Threads REQUIRED)

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

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(server PRIVATE
            -Wa,-mbig-obj
            -O2
        )
    endif()

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

    target_compile_options(server PRIVATE
        -O2
        -pthread
    )

    find_package(PostgreSQL REQUIRED)
    if (TARGET PostgreSQL::PostgreSQL)
        target_link_libraries(server PRIVATE PostgreSQL::PostgreSQL)
    else()
        target_include_directories(server PRIVATE ${PostgreSQL_INCLUDE_DIRS})
        target_link_libraries(server PRIVATE ${PostgreSQL_LIBRARIES})
    endif()

    target_link_libraries(server PRIVATE
        Threads::Threads
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
        if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(stress_client PRIVATE
                -Wa,-mbig-obj
                -O2
            )
        endif()
        target_link_libraries(stress_client PRIVATE
            Threads::Threads
            ws2_32
            mswsock
            stdc++exp
            psapi
        )
    else()
        target_compile_options(stress_client PRIVATE
            -O2
            -pthread
        )
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