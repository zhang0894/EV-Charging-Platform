# qmake 工程（机房/老师环境备用；平时用 CMakeLists.txt）
# 测试程序 test_cloudflow 只在 CMake 工程里
QT       += core gui widgets network
CONFIG   += c++17
TARGET    = ncs_client_cloud
TEMPLATE  = app

INCLUDEPATH += $$PWD

# 有 WebEngine 模块就启用内嵌腾讯地图，没有也能编译（导航页降级）
qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += NCS_HAS_WEBENGINE
}

SOURCES += \
    main.cpp \
    core/apiclient.cpp \
    core/userservice.cpp \
    core/chargeservice.cpp \
    ui/mainwindow.cpp \
    ui/chargepage.cpp \
    ui/orderspage.cpp \
    ui/loginwindow.cpp \
    ui/firstprofilesetuppage.cpp \
    ui/stationlistpage.cpp \
    ui/profilepage.cpp \
    ui/navigationpage.cpp

HEADERS += \
    core/session.h \
    core/apiclient.h \
    core/userservice.h \
    core/chargeservice.h \
    ui/theme.h \
    ui/placeholderpage.h \
    ui/receipttext.h \
    ui/mainwindow.h \
    ui/chargepage.h \
    ui/orderspage.h \
    ui/loginwindow.h \
    ui/firstprofilesetuppage.h \
    ui/stationlistpage.h \
    ui/profilepage.h \
    ui/navigationpage.h

RESOURCES += assets.qrc
