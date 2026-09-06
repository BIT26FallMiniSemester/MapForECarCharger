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
