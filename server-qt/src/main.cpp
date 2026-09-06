#include "server.h"
#include <iostream>

int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("charger-server");QCoreApplication::setApplicationVersion("0.4.0");
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"database","SQLite database path","path",qEnvironmentVariable("DATABASE_PATH","runtime/charger-qt.db")});
    parser.addOption({"host","Listen address","host",qEnvironmentVariable("SERVER_HOST","127.0.0.1")});
    parser.addOption({"port","TCP port","port",qEnvironmentVariable("SERVER_PORT","9000")});
    parser.addOption({"migrate-only","Apply migrations and exit"});
    parser.addOption({"import-legacy","Import supported Python database into empty target and exit","source"});
    parser.addOption({"import-catalog","Import cleaned public station JSON and exit","source"});
    parser.addOption({"catalog-id-source","Preserve station IDs from a read-only legacy database","database"});
    parser.addOption({"create-admin","Create administrator using INITIAL_ADMIN_PASSWORD environment variable and exit","username"});
    parser.addOption({"seed-demo","Create idempotent Qt-only demonstration data (admin/admin123)"});
    parser.process(app);
    try{
        Database db(QFileInfo(parser.value("database")).absoluteFilePath());db.migrate();
        if(parser.isSet("import-legacy")){db.importLegacy(parser.value("import-legacy"));std::cout<<"Legacy import complete; source unchanged\n";return 0;}
        if(parser.isSet("catalog-id-source")&&!parser.isSet("import-catalog"))fail(40001);
        if(parser.isSet("import-catalog")){db.importCatalog(parser.value("import-catalog"),parser.value("catalog-id-source"));std::cout<<"Catalog import complete\n";return 0;}
        if(parser.isSet("create-admin")){db.createAdmin(parser.value("create-admin"),qEnvironmentVariable("INITIAL_ADMIN_PASSWORD"));std::cout<<"Administrator initialized (existing credentials preserved)\n";return 0;}
        if(parser.isSet("seed-demo")){db.seedDemo();std::cout<<"Qt demo data ready (admin/admin123, user 13900000000)\n";}
        if(parser.isSet("migrate-only"))return 0;
        bool ok=false;int port=parser.value("port").toInt(&ok);QHostAddress address(parser.value("host"));if(!ok||port<1||port>65535||address.isNull())fail(40001);
        Server server(db,qEnvironmentVariable("TENCENT_MAP_KEY"));if(!server.listen(address,quint16(port)))fail(50000);
        qInfo().noquote()<<"Qt Socket server listening on"<<server.serverAddress().toString()<<server.serverPort();return app.exec();
    }catch(const Failure &e){std::cerr<<"Startup failed, code="<<e.code<<" (check database version, permissions and arguments)\n";return 1;}
}
