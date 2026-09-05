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
