# ==========================================================
# 充电桩运营管理平台 - CMake 构建脚本
# 兼容 Linux / Windows，链接 Qt6 Core/Widgets/Charts/Network
#
# 项目结构：
#   src/        源文件 (.cpp / .h / .ui)
#   resources/  资源文件 (.qrc / .qss / img)
#   docs/       文档
# ==========================================================

cmake_minimum_required(VERSION 3.16)

project(ChargingPileAdmin VERSION 1.0.0 LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 自动化处理：MOC(元对象) / UIC(.ui) / RCC(.qrc)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 查找 Qt6 所需模块
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Charts Network)

# 应用程序目标
add_executable(ChargingPileAdmin
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/dashboardwidget.cpp
    src/dashboardwidget.h
    src/dashboardwidget.ui
    src/dashboardmodel.cpp
    src/dashboardmodel.h
    src/logindialog.cpp
    src/logindialog.h
    src/models/pilestatusmodel.cpp
    src/models/pilestatusmodel.h
    src/models/usermanagementmodel.cpp
    src/models/usermanagementmodel.h
    src/widgets/pilestatuswidget.cpp
    src/widgets/pilestatuswidget.h
    src/widgets/usermanagementwidget.cpp
    src/widgets/usermanagementwidget.h
    resources/resources.qrc
)

# 将 src 目录加入包含路径：
# mainwindow.ui 把 DashboardWidget 提升为自定义控件，
# 生成的 ui_mainwindow.h 会 #include "dashboardwidget.h"，需要能定位到它。
target_include_directories(ChargingPileAdmin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/models
    ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets
)

# 链接 Qt6 模块
target_link_libraries(ChargingPileAdmin PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Charts
    Qt6::Network
)

# 可执行文件输出到 build/bin
set_target_properties(ChargingPileAdmin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Windows 下默认保持控制台子系统，便于 qDebug 输出可见。
# 如需纯 GUI（无控制台窗口），取消下方注释。
# if(WIN32)
#     set_target_properties(ChargingPileAdmin PROPERTIES WIN32_EXECUTABLE TRUE)
# endif()
