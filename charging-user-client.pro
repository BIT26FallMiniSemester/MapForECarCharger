# 导航页用 QWebEngineView 加载腾讯地图。Windows 请用 MSVC 套件（MinGW 通常没有 WebEngine）。
QT += core gui widgets network webenginewidgets

CONFIG += c++17
TEMPLATE = app
TARGET = charging-user-client

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    apiclient.cpp \
    loginpage.cpp \
    homepage.cpp \
    profilepage.cpp \
    locationdialog.cpp \
    tencentgeocoder.cpp \
    stationdetaildialog.cpp \
    navigationdialog.cpp \
    rechargedialog.cpp \
    recordlistdialog.cpp \
    editprofiledialog.cpp

HEADERS += \
    mainwindow.h \
    apiclient.h \
    models.h \
    loginpage.h \
    homepage.h \
    profilepage.h \
    locationdialog.h \
    tencentgeocoder.h \
    mapconfig.h \
    webenginesetup.h \
    stationdetaildialog.h \
    navigationdialog.h \
    rechargedialog.h \
    recordlistdialog.h \
    editprofiledialog.h

FORMS += \
    mainwindow.ui \
    loginpage.ui \
    homepage.ui \
    profilepage.ui \
    locationdialog.ui \
    stationdetaildialog.ui \
    navigationdialog.ui \
    rechargedialog.ui \
    recordlistdialog.ui \
    editprofiledialog.ui
