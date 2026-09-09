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
# Chromium/WebEngine may terminate the whole Qt widget process on machines
# without a compatible sandbox/GPU backend.  Keep the route page lightweight
# by default; enable the embedded map explicitly on supported deployments.
option(NCS_ENABLE_WEBENGINE "Enable embedded Tencent map" ON)
if (NCS_ENABLE_WEBENGINE)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS WebEngineWidgets QUIET)
endif()

add_executable(ncs_client_cloud
    main.cpp
    assets.qrc
    core/session.h
    core/apiclient.h           core/apiclient.cpp
    core/userservice.h         core/userservice.cpp
    core/chargeservice.h       core/chargeservice.cpp
    ui/mainwindow.cpp          ui/mainwindow.h
    ui/theme.h
    ui/socgauge.h
    ui/carart.h
    ui/placeholderpage.h
    ui/chargepage.h            ui/chargepage.cpp
    ui/receipttext.h
    ui/orderspage.h            ui/orderspage.cpp
    ui/loginwindow.h           ui/loginwindow.cpp
    ui/firstprofilesetuppage.h ui/firstprofilesetuppage.cpp
    ui/stationlistpage.h       ui/stationlistpage.cpp
    ui/profilepage.h           ui/profilepage.cpp
    ui/navigationpage.h        ui/navigationpage.cpp
)
if (TARGET Qt${QT_VERSION_MAJOR}::WebEngineWidgets)
    target_link_libraries(ncs_client_cloud PRIVATE Qt${QT_VERSION_MAJOR}::WebEngineWidgets)
    target_compile_definitions(ncs_client_cloud PRIVATE NCS_HAS_WEBENGINE)
endif()
target_include_directories(ncs_client_cloud PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ncs_client_cloud PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

# 云端流程测试（命令行，直接打真实 server）：NCS_PHONE=13800000002 NCS_PHONE_B=13800000003 ./test_cloudflow
add_executable(test_cloudflow
    tests/test_cloudflow.cpp
    core/session.h
    core/apiclient.h           core/apiclient.cpp
    core/userservice.h         core/userservice.cpp
    core/chargeservice.h       core/chargeservice.cpp
)
target_include_directories(test_cloudflow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_cloudflow PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)

message(STATUS "ncs_client_cloud: Qt ${QT_VERSION_MAJOR} (${QT_VERSION})")
