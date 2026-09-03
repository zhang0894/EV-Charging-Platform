cmake_minimum_required(VERSION 3.16)
project(ncs_client_cloud LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 云端版：不需要 Sql，多了 Network（socket 通信走 QNetworkAccessManager）
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

add_executable(ncs_client_cloud
    main.cpp
    core/session.h
    core/apiclient.h           core/apiclient.cpp
    core/chargeservice.h       core/chargeservice.cpp
    ui/mainwindow.cpp          ui/mainwindow.h
    ui/theme.h
    ui/placeholderpage.h
    ui/chargepage.h            ui/chargepage.cpp
)
target_include_directories(ncs_client_cloud PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ncs_client_cloud PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

message(STATUS "ncs_client_cloud: Qt ${QT_VERSION_MAJOR} (${QT_VERSION})")
