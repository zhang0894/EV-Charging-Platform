cmake_minimum_required(VERSION 3.16)
project(ncs_user LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)


find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql)

add_executable(ncs_user
    main.cpp
    ui/mainwindow.cpp          ui/mainwindow.h
    ui/theme.h
    ui/placeholderpage.h
    core/session.h
    resources.qrc
    core/databasemanager.h
    core/databasemanager.cpp
    core/chargeservice.h
    core/chargeservice.cpp
    ui/chargepage.h
    ui/chargepage.cpp
)
target_include_directories(ncs_user PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ncs_user PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
)

message(STATUS "ncs_user: Qt ${QT_VERSION_MAJOR} (${QT_VERSION})")