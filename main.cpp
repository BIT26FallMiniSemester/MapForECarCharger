#include <QApplication>
#include <QCoreApplication>
#include <QtGlobal>

#include "mainwindow.h"
#include "webenginesetup.h"

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#endif
    // 必须在 QApplication 之前：虚拟机里找不到 QtWebEngineProcess 会直接 abort
    setupWebEngineEnvironment();
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("充电用户端"));
    app.setOrganizationName(QStringLiteral("EVCharge"));

    MainWindow window;
    window.show();
    return app.exec();
}
