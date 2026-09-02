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

target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../boost_1_92_0
    ${CMAKE_CURRENT_SOURCE_DIR}/../glaze-8.3.0/include
    "C:/Program Files/PostgreSQL/18/include"
)

target_compile_definitions(server PRIVATE
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(server PRIVATE
    -Wa,-mbig-obj
    -O2
)

target_link_libraries(server PRIVATE
    Threads::Threads
    "C:/Program Files/PostgreSQL/18/bin/libpq.dll"
    "C:/Program Files/PostgreSQL/18/bin/libcrypto-3-x64.dll"
    "C:/Program Files/PostgreSQL/18/bin/libssl-3-x64.dll"
    ws2_32
    mswsock
    stdc++exp
)