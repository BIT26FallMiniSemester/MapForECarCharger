// 服务端程序入口，解析启动参数、执行迁移/初始化并监听 TCP 端口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("充电用户端"));
    app.setOrganizationName(QStringLiteral("EVCharge"));

    MainWindow window;
    window.show();
    return app.exec();
}
