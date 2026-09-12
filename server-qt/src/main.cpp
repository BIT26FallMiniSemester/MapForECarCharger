// 服务端程序入口，解析启动参数、执行迁移/初始化并监听 TCP 端口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "server.h"
#include "dashboardserver.h"
#include <iostream>

/// 实现 main 的本地处理逻辑，保持与项目其他模块的接口约定一致。
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("charger-server");QCoreApplication::setApplicationVersion("0.4.0");
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"database","SQLite database path","path",qEnvironmentVariable("DATABASE_PATH","runtime/charger-qt.db")});
    parser.addOption({"host","Listen address","host",qEnvironmentVariable("SERVER_HOST","127.0.0.1")});
    parser.addOption({"port","TCP port","port",qEnvironmentVariable("SERVER_PORT","9000")});
    parser.addOption({"dashboard-port","Dashboard HTTP port","port",qEnvironmentVariable("DASHBOARD_PORT","9001")});
    parser.addOption({"migrate-only","Apply migrations and exit"});
    parser.addOption({"import-catalog","Import cleaned public station JSON and exit","source"});
    parser.addOption({"catalog-id-source","Preserve station IDs from a read-only existing catalog database","database"});
    parser.addOption({"create-admin","Create administrator using INITIAL_ADMIN_PASSWORD environment variable and exit","username"});
    parser.addOption({"seed-demo","Create idempotent Qt-only demonstration data (admin/123456)"});
    parser.addOption({"seed-showcase","Add one test user and all-IDLE managed piles to an imported Beijing catalog"});
    parser.process(app);
    try{
        Database db(QFileInfo(parser.value("database")).absoluteFilePath());db.migrate();
        if(parser.isSet("catalog-id-source")&&!parser.isSet("import-catalog"))fail(40001);
        if(parser.isSet("import-catalog")){db.importCatalog(parser.value("import-catalog"),parser.value("catalog-id-source"));std::cout<<"Catalog import complete\n";return 0;}
        if(parser.isSet("create-admin")){db.createAdmin(parser.value("create-admin"),qEnvironmentVariable("INITIAL_ADMIN_PASSWORD"));std::cout<<"Administrator initialized (existing credentials preserved)\n";return 0;}
        if(parser.isSet("seed-demo")){db.seedDemo();std::cout<<"Qt demo data ready (admin/123456, user 13900000000)\n";}
        if(parser.isSet("seed-showcase")){db.seedShowcase();std::cout<<"Real Beijing showcase ready (admin/123456, user 13900000000)\n";}
        if(parser.isSet("migrate-only"))return 0;
        bool ok=false,dashboardOk=false;int port=parser.value("port").toInt(&ok);int dashboardPort=parser.value("dashboard-port").toInt(&dashboardOk);QHostAddress address(parser.value("host"));if(!ok||port<1||port>65535||!dashboardOk||dashboardPort<1||dashboardPort>65535||dashboardPort==port||address.isNull())fail(40001);
        Server server(db,qEnvironmentVariable("TENCENT_MAP_KEY"));if(!server.listen(address,quint16(port)))fail(50000);
        DashboardServer dashboard(server.business);if(!dashboard.listen(address,quint16(dashboardPort)))fail(50000);
        qInfo().noquote()<<"Qt Socket server listening on"<<server.serverAddress().toString()<<server.serverPort()<<"dashboard"<<dashboard.serverPort();return app.exec();
    }catch(const Failure &e){std::cerr<<"Startup failed, code="<<e.code<<" (check database version, permissions and arguments)\n";return 1;}
}
