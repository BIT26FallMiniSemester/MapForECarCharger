QT += core gui widgets network

CONFIG += c++17
TEMPLATE = app
TARGET = charging-user-client

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    chargingpage.cpp \
    apiclient.cpp \
    loginpage.cpp \
    homepage.cpp \
    profilepage.cpp \
    locationdialog.cpp \
    ../common/socketclient.cpp

HEADERS += \
    mainwindow.h \
    chargingpage.h \
    apiclient.h \
    models.h \
    loginpage.h \
    homepage.h \
    profilepage.h \
    locationdialog.h \
    ../common/socketclient.h

INCLUDEPATH += ../common

FORMS += \
    mainwindow.ui \
    loginpage.ui \
    homepage.ui \
    profilepage.ui \
    locationdialog.ui
