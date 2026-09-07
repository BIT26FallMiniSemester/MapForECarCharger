// 服务端程序入口，解析启动参数、执行迁移/初始化并监听 TCP 端口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "loginwindow.h"
#include <QApplication>
#include <QFile>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("充电林运营管理平台");
    app.setOrganizationName("NeusoftCharging");
    QFile style(":/styles/style.qss");
    if (style.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));
    LoginWindow window;
    window.show();
    return app.exec();
}
