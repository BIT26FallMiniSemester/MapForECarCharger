QT += core gui widgets charts network
CONFIG += c++17
TEMPLATE = app
TARGET = ChargingAdmin

SOURCES += \
    main.cpp \
    loginwindow.cpp \
    mainwindow.cpp \
    apiclient.cpp \
    mockrepository.cpp

HEADERS += \
    loginwindow.h \
    mainwindow.h \
    apiclient.h \
    mockrepository.h

FORMS += \
    loginwindow.ui \
    mainwindow.ui

RESOURCES += resources.qrc

unix: target.path = /opt/ChargingAdmin
!isEmpty(target.path): INSTALLS += target
