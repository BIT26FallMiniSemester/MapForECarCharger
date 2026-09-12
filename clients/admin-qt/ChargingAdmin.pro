# 配置管理端 Qt 源文件、Qt 模块、资源文件和构建目标。

QT += core gui widgets charts network
CONFIG += c++17
TEMPLATE = app
TARGET = ChargingAdmin

SOURCES += \
    main.cpp \
    loginwindow.cpp \
    mainwindow.cpp \
    apiclient.cpp \
    mockrepository.cpp \
    ../common/socketclient.cpp

HEADERS += \
    loginwindow.h \
    mainwindow.h \
    apiclient.h \
    mockrepository.h \
    ../common/socketclient.h

INCLUDEPATH += ../common

FORMS += \
    loginwindow.ui \
    mainwindow.ui

RESOURCES += resources.qrc

unix: target.path = /opt/ChargingAdmin
!isEmpty(target.path): INSTALLS += target
