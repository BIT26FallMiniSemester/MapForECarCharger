#include <QtTest>
#include <QtEndian>
#include "server.h"
#include <memory>

struct Peer {
    QTcpSocket socket;QByteArray buffer;
    explicit Peer(quint16 port){socket.connectToHost(QHostAddress::LocalHost,port);if(!socket.waitForConnected(1000))fail(50000);}
    void send(QJsonObject request){socket.write(frame(request));socket.flush();}
    QJsonObject receive(){
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<3000){
            QCoreApplication::processEvents();buffer+=socket.readAll();
            if(buffer.size()>=4){auto n=qFromBigEndian<quint32>(buffer.constData());if(buffer.size()>=n+4){auto doc=QJsonDocument::fromJson(buffer.mid(4,n));buffer.remove(0,n+4);return doc.object();}}
            QTest::qWait(1);
        }fail(50000);
    }
};
class Tests:public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<Database> db;
    std::unique_ptr<Server> server;
    std::unique_ptr<Peer> peer;
    QString token,admin;int sequence=0;
    QJsonObject request(QString a,QJsonObject d={},QString auth={}){QJsonObject q{{"version",1},{"request_id","test_"+QString::number(++sequence)},{"action",a},{"data",d}};if(!auth.isEmpty())q["token"]=auth;return q;}
    QJsonObject call(QString a,QJsonObject d={},QString auth={}) {
        auto q=request(a,d,auth);peer->send(q);auto r=peer->receive();
        if(r["request_id"]!=q["request_id"]||r["action"]!=a)QTest::qFail("Response correlation mismatch",__FILE__,__LINE__);
        if(r["code"]==0&&!validate(r["data"],contract()["x-actions"].toObject()[a].toObject()["response"].toObject())){qWarning().noquote()<<"Response contract mismatch"<<a;QTest::qFail("Response schema mismatch",__FILE__,__LINE__);}
        return r;
    }
    qint64 create(){auto r=call("orders.create",{{"station_id",1},{"pile_id",1}},token);return r["data"].toObject()["id"].toInteger();}
    void start(qint64 id){QCOMPARE(call("orders.reserve",{{"order_id",id}},token)["code"].toInt(),0);QCOMPARE(call("orders.start",{{"order_id",id}},token)["code"].toInt(),0);}
private slots:
    void init(){
        dir=std::make_unique<QTemporaryDir>();db=std::make_unique<Database>(dir->filePath("test.db"));db->migrate();
        db->createAdmin("admin","test-password");
        db->execute("INSERT INTO stations(id,name,address,latitude,longitude,price_cents_per_kwh,created_at,updated_at) VALUES(1,'测试站','测试地址',39.95,116.32,160,?,?)",{utcNow(),utcNow()});
        db->execute("INSERT INTO charging_piles(id,station_id,pile_no,charge_type,rated_power_w,created_at,updated_at) VALUES(1,1,'TEST-1','FAST',60000,?,?)",{utcNow(),utcNow()});
        server=std::make_unique<Server>(*db,QString());QVERIFY(server->listen(QHostAddress::LocalHost,0));peer=std::make_unique<Peer>(server->serverPort());
        token=call("auth.user.login",{{"phone","13900000001"}})["data"].toObject()["access_token"].toString();
        admin=call("auth.admin.login",{{"username","admin"},{"password","test-password"}})["data"].toObject()["access_token"].toString();
        QVERIFY(!token.isEmpty());QVERIFY(!admin.isEmpty());
    }
    void cleanup(){peer.reset();server.reset();db.reset();dir.reset();}
    void protocolAndPermissions(){
        QCOMPARE(contract()["x-actions"].toObject().size(),41);
        QCOMPARE(call("system.health")["code"].toInt(),0);
        QCOMPARE(call("orders.active",{},token)["data"],QJsonValue(QJsonValue::Null));
        QCOMPARE(call("users.me.get")["code"].toInt(),40101);
        QCOMPARE(call("admin.overview",{},token)["code"].toInt(),40301);
        QCOMPARE(call("users.me.get",{},admin)["code"].toInt(),40301);
        QCOMPARE(call("unknown.action")["code"].toInt(),40009);
        QCOMPARE(call("users.me.update",{{"nickname","  中文昵称  "}},token)["data"].toObject()["nickname"].toString(),QStringLiteral("中文昵称"));
        QCOMPARE(call("users.me.update",{{"nickname","   "}},token)["code"].toInt(),40001);
        QCOMPARE(call("orders.create",{{"station_id",1},{"pile_id",1.5}},token)["code"].toInt(),40001);
        QCOMPARE(call("orders.create",{{"station_id",1},{"pile_id",1},{"user_id",2}},token)["code"].toInt(),40001);
        QCOMPARE(call("auth.admin.login",{{"username","admin"},{"password","bad"}})["code"].toInt(),40101);
        auto q=request("system.health");q["version"]=2;peer->send(q);QCOMPARE(peer->receive()["code"].toInt(),40010);
    }
    void fragmentedAndCoalescedFrames(){
        auto a=request("users.me.update",{{"nickname","中文分帧"}},token),b=request("system.health");auto bytes=frame(a);
        peer->socket.write(bytes.left(2));peer->socket.flush();QTest::qWait(10);QCOMPARE(peer->socket.bytesAvailable(),0);
        peer->socket.write(bytes.mid(2,7));peer->socket.flush();QTest::qWait(10);QCOMPARE(peer->socket.bytesAvailable(),0);
        peer->socket.write(bytes.mid(9)+frame(b));peer->socket.flush();
        auto r1=peer->receive(),r2=peer->receive();QCOMPARE(r1["request_id"],a["request_id"]);QCOMPARE(r2["request_id"],b["request_id"]);
        QCOMPARE(r1["data"].toObject()["nickname"].toString(),QStringLiteral("中文分帧"));
    }
    void malformedFramesCloseConnection(){
        for(auto bytes:QList<QByteArray>{QByteArray::fromHex("00000000"),QByteArray::fromHex("00100001"),QByteArray::fromHex("00000001ff"),QByteArray::fromHex("000000027b7d")}){
            Peer other(server->serverPort());other.socket.write(bytes);other.socket.flush();QTRY_COMPARE(other.socket.state(),QAbstractSocket::UnconnectedState);
        }
        QCOMPARE(call("system.health")["code"].toInt(),0);
    }
    void walletIdempotenceAndRollback(){
        auto data=QJsonObject{{"client_request_id","recharge-test-0001"},{"amount_cents",5000}};
        auto first=call("wallet.recharges.create",data,token);QCOMPARE(first["code"].toInt(),0);auto second=call("wallet.recharges.create",data,token);QCOMPARE(second["code"].toInt(),0);QCOMPARE(first["data"],second["data"]);
        data["amount_cents"]=6000;QCOMPARE(call("wallet.recharges.create",data,token)["code"].toInt(),40001);
        QCOMPARE(call("users.me.get",{},token)["data"].toObject()["balance_cents"].toInt(),5000);
        QCOMPARE(call("wallet.recharges.list",{},token)["data"].toObject()["total"].toInt(),1);
        auto other=call("auth.user.login",{{"phone","13900000002"}})["data"].toObject()["access_token"].toString();
        data["amount_cents"]=5000;QCOMPARE(call("wallet.recharges.create",data,other)["code"].toInt(),40301);
        db->execute("CREATE TRIGGER reject_recharge BEFORE INSERT ON recharge_records BEGIN SELECT RAISE(ABORT,'test rollback'); END");
        data["client_request_id"]="recharge-test-0002";QCOMPARE(call("wallet.recharges.create",data,token)["code"].toInt(),50001);QCOMPARE(db->scalar("SELECT balance_cents FROM users WHERE id=1"),5000);
    }
    void chargingFlowAndSettlement(){
        auto id=create();QVERIFY(id>0);QCOMPARE(create(),0);start(id);
        const QJsonValue started=db->one("SELECT started_at FROM charging_orders WHERE id=?",{id})["started_at"];
        QCOMPARE(call("orders.start",{{"order_id",id}},token)["code"].toInt(),40004);QCOMPARE(db->one("SELECT started_at FROM charging_orders WHERE id=?",{id})["started_at"],started);
        db->execute("UPDATE charging_orders SET started_at=? WHERE id=?",{QDateTime::currentDateTimeUtc().addSecs(-1800).toString(Qt::ISODateWithMs),id});
        auto progress=call("orders.detail",{{"order_id",id}},token)["data"].toObject();QVERIFY(progress["estimated"].toBool());QCOMPARE(progress["energy_wh"].toInteger(),30000);
        QCOMPARE(call("orders.cancel",{{"order_id",id}},token)["code"].toInt(),40004);
        QJsonValue stop=call("orders.stop",{{"order_id",id}},token)["data"];QCOMPARE(call("orders.stop",{{"order_id",id}},token)["data"],stop);
        QCOMPARE(db->scalar("SELECT count(*) FROM charging_piles WHERE id=1 AND status='IDLE' AND reserved_order_id IS NULL"),1);
        QCOMPARE(call("orders.settle",{{"order_id",id}},token)["code"].toInt(),40006);QCOMPARE(db->scalar("SELECT balance_cents FROM users WHERE id=1"),0);
        QCOMPARE(db->one("SELECT status FROM charging_orders WHERE id=?",{id})["status"].toString(),QString("UNPAID"));
        call("wallet.recharges.create",{{"client_request_id","flow-recharge-01"},{"amount_cents",10000}},token);
        auto paid=call("orders.settle",{{"order_id",id}},token);QCOMPARE(paid["code"].toInt(),0);QCOMPARE(paid["data"],call("orders.settle",{{"order_id",id}},token)["data"]);
        QCOMPARE(db->scalar("SELECT balance_cents FROM users WHERE id=1"),5200);
        QCOMPARE(call("admin.overview",{},admin)["data"].toObject()["total_revenue_cents"].toInteger(),4800);
        QCOMPARE(call("orders.list",{},token)["data"].toObject()["total"].toInt(),1);
        auto trend=call("admin.revenue_trend",{{"days",30}},admin)["data"].toObject();QCOMPARE(trend["items"].toArray().size(),30);
        QCOMPARE(call("admin.pile_status",{},admin)["data"].toObject()["IDLE"].toInt(),1);
    }
    void twoConnectionsCompete(){
        auto token2=call("auth.user.login",{{"phone","13900000002"}})["data"].toObject()["access_token"].toString();auto id1=create();
        auto id2=call("orders.create",{{"station_id",1},{"pile_id",1}},token2)["data"].toObject()["id"].toInteger();Peer other(server->serverPort());
        peer->send(request("orders.reserve",{{"order_id",id1}},token));other.send(request("orders.reserve",{{"order_id",id2}},token2));
        int a=peer->receive()["code"].toInt(),b=other.receive()["code"].toInt();QVERIFY((a==0&&b==40003)||(b==0&&a==40003));
        QCOMPARE(db->scalar("SELECT count(*) FROM charging_orders WHERE status='RESERVED'"),1);
        QCOMPARE(call("orders.detail",{{"order_id",id2}},token)["code"].toInt(),40301);
    }
    void cancellationExpiryAndFrozenUser(){
        auto id=create();call("orders.reserve",{{"order_id",id}},token);
        QCOMPARE(call("admin.users.freeze",{{"user_id",1}},admin)["code"].toInt(),40007);
        db->execute("UPDATE charging_orders SET expires_at=? WHERE id=?",{QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODateWithMs),id});
        QCOMPARE(call("orders.start",{{"order_id",id}},token)["code"].toInt(),40005);
        server->business.expire();QCOMPARE(db->scalar("SELECT count(*) FROM pile_status_logs WHERE reason='EXPIRE'"),1);
        id=create();call("orders.reserve",{{"order_id",id}},token);QCOMPARE(call("orders.cancel",{{"order_id",id}},token)["code"].toInt(),0);
        QCOMPARE(call("admin.users.freeze",{{"user_id",1}},admin)["code"].toInt(),0);
        QCOMPARE(call("users.me.get",{},token)["code"].toInt(),40301);
        QCOMPARE(call("auth.user.login",{{"phone","13900000001"}})["code"].toInt(),40301);
        QCOMPARE(call("admin.users.unfreeze",{{"user_id",1}},admin)["code"].toInt(),0);
    }
    void adminAndCatalogActions(){
        QCOMPARE(call("stations.list",{},token)["code"].toInt(),0);QCOMPARE(call("stations.detail",{{"station_id",1}},token)["code"].toInt(),0);
        QCOMPARE(call("stations.piles.list",{{"station_id",1}},token)["code"].toInt(),0);QCOMPARE(call("piles.detail",{{"pile_id",1}},token)["code"].toInt(),0);
        QCOMPARE(call("stations.detail",{{"station_id",999}},token)["code"].toInt(),40401);
        auto s=call("admin.stations.create",{{"name","新测试站"},{"address","测试地址"},{"latitude",39.96},{"longitude",116.32},{"price_cents_per_kwh",200}},admin)["data"].toObject();auto id=s["id"].toInteger();QVERIFY(id>1);
        QCOMPARE(call("admin.stations.update",{{"station_id",id},{"price_cents_per_kwh",180}},admin)["code"].toInt(),0);
        auto p=QJsonObject{{"station_id",id},{"pile_no","NEW-1"},{"charge_type","SLOW"},{"rated_power_w",7000}};
        auto pileId=call("admin.piles.create",p,admin)["data"].toObject()["id"].toInteger();QVERIFY(pileId>1);QCOMPARE(call("admin.piles.create",p,admin)["code"].toInt(),40008);
        QCOMPARE(call("admin.piles.recover",{{"pile_id",pileId}},admin)["code"].toInt(),40004);
        db->execute("UPDATE charging_piles SET status='FAULT' WHERE id=?",{pileId});QCOMPARE(call("admin.piles.recover",{{"pile_id",pileId}},admin)["code"].toInt(),0);
        for(const auto &a:QStringList{"admin.users.list","admin.stations.list","admin.piles.list","admin.orders.list"})QCOMPARE(call(a,{},admin)["code"].toInt(),0);
        QCOMPARE(call("admin.users.detail",{{"user_id",1}},admin)["code"].toInt(),0);QCOMPARE(call("admin.piles.detail",{{"pile_id",pileId}},admin)["code"].toInt(),0);
        QCOMPARE(call("admin.revenue_trend",{{"days",6}},admin)["code"].toInt(),40001);
        QVERIFY(db->scalar("SELECT count(*) FROM operation_logs")>=4);
    }
    void avatarAndReconnect(){
        auto encoded=QString::fromLatin1(QByteArray::fromHex("89504e470d0a1a0a000000").toBase64());
        QJsonValue avatar=call("users.avatar.set",{{"content_type","image/png"},{"content_base64",encoded}},token)["data"].toObject()["avatar_id"];
        QCOMPARE(call("users.avatar.get",{{"avatar_id",avatar}},token)["data"].toObject()["content_base64"].toString(),encoded);
        QCOMPARE(call("users.avatar.get",{{"avatar_id","../file"}},token)["code"].toInt(),40001);
        peer.reset();peer=std::make_unique<Peer>(server->serverPort());QCOMPARE(call("users.me.get",{},token)["code"].toInt(),0);
        peer->send(request("wallet.recharges.create",{{"client_request_id","lost-response-01"},{"amount_cents",5000}},token));QTest::qWait(20);peer.reset();peer=std::make_unique<Peer>(server->serverPort());
        QCOMPARE(call("wallet.recharges.create",{{"client_request_id","lost-response-01"},{"amount_cents",5000}},token)["data"].toObject()["balance_cents"].toInt(),5000);
        peer.reset();server.reset();server=std::make_unique<Server>(*db,QString());QVERIFY(server->listen(QHostAddress::LocalHost,0));peer=std::make_unique<Peer>(server->serverPort());QCOMPARE(call("users.me.get",{},token)["code"].toInt(),40101);
    }
    void mapMissingKeyFailsWithoutFabricatedDistance(){
        QCOMPARE(call("map.geocode",{{"address","北京"}},token)["code"].toInt(),50301);
        QCOMPARE(call("map.geocode",{{"address","北京"}},admin)["code"].toInt(),50301);
        QCOMPARE(call("map.route",{{"from_latitude",39.95},{"from_longitude",116.32},{"to_latitude",39.96},{"to_longitude",116.33}},token)["code"].toInt(),50301);
        QCOMPARE(call("map.snapshot",{{"latitude",39.95},{"longitude",116.32}},token)["code"].toInt(),50301);
        QCOMPARE(call("stations.nearby",{{"latitude",39.95},{"longitude",116.32},{"radius_km",1}},token)["code"].toInt(),50301);
    }
    void mapHttpParsingAndTimeout(){
        QTcpServer upstream;QVERIFY(upstream.listen(QHostAddress::LocalHost,0));QString mode="geocode";
        connect(&upstream,&QTcpServer::newConnection,&upstream,[&]{auto socket=upstream.nextPendingConnection();connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{auto input=socket->readAll();if(!input.contains("\r\n\r\n"))return;if(mode=="timeout")return;if(mode=="snapshot"){const auto bytes=QByteArray::fromHex("89504e470d0a1a0a");socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+bytes);socket->disconnectFromHost();return;}
            QJsonObject response{{"status",0}};
            if(mode=="badkey")response["status"]=110;
            else if(mode=="geocode")response["result"]=QJsonObject{{"location",QJsonObject{{"lat",39.95},{"lng",116.32}}},{"title","北京"}};
            else if(mode=="route")response["result"]=QJsonObject{{"routes",QJsonArray{QJsonObject{{"distance",1200},{"duration",10},{"polyline",QJsonArray{39.95,116.32,1000,2000}}}}}};
            else if(mode=="matrix")response["result"]=QJsonObject{{"rows",QJsonArray{QJsonObject{{"elements",QJsonArray{QJsonObject{{"distance",2300},{"duration",300}},QJsonObject{{"distance",1},{"status",4}}}}}}}};
            else response["result"]=QJsonObject{{"routes",QJsonArray{}}};
            auto bytes=QJsonDocument(response).toJson(QJsonDocument::Compact);socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+bytes);socket->disconnectFromHost();});});
        Maps maps("test-key",nullptr,QUrl("http://127.0.0.1:"+QString::number(upstream.serverPort())),100,500);bool done=false;int code=-1;QJsonValue data;
        auto callback=[&](QJsonValue d,int c){data=d;code=c;done=true;};
        maps.run("map.geocode",{{"address","北京"}},{},callback);QTRY_VERIFY(done);QCOMPARE(code,0);QCOMPARE(data.toObject()["latitude"].toDouble(),39.95);
        mode="route";done=false;maps.run("map.route",{{"from_latitude",39.95},{"from_longitude",116.32},{"to_latitude",39.96},{"to_longitude",116.33}},{},callback);QTRY_VERIFY(done);QCOMPARE(code,0);QCOMPARE(data.toObject()["duration_seconds"].toInt(),600);
        QCOMPARE(data.toObject()["route_points"].toArray()[1].toObject()["latitude"].toDouble(),39.951);
        mode="matrix";done=false;auto s=server->business.station(1);maps.run("stations.nearby",{{"latitude",39.95},{"longitude",116.32}},QJsonArray{s,s},callback);QTRY_VERIFY(done);QCOMPARE(code,0);QCOMPARE(data.toObject()["total"].toInt(),1);
        mode="snapshot";done=false;maps.run("map.snapshot",{{"latitude",39.95},{"longitude",116.32}}, {},callback);QTRY_VERIFY(done);QCOMPARE(code,0);QVERIFY(!data.toObject()["content_base64"].toString().isEmpty());
        for(const auto &behavior:QStringList{"badkey","empty","timeout"}){mode=behavior;done=false;maps.run("map.route",{}, {},callback);QTRY_VERIFY(done);QCOMPARE(code,50301);}
    }
    void databaseMigrationsAndCalculation(){
        db->migrate();QCOMPARE(db->scalar("SELECT count(*) FROM schema_migrations"),2);QVERIFY(db->rows("PRAGMA foreign_key_check").isEmpty());
        QCOMPARE(roundedProduct(1,1800,3600),1);QCOMPARE(roundedProduct(60000,1800,3600),30000);
        QVERIFY_EXCEPTION_THROWN(roundedProduct(LLONG_MAX,2,1),Failure);
        auto hash=passwordHash("hello-world");QVERIFY(passwordVerify("hello-world",hash));QVERIFY(!passwordVerify("wrong",hash));QVERIFY(!passwordVerify("x","malformed"));
        db->createAdmin("admin","different-password");QVERIFY(passwordVerify("test-password",db->one("SELECT password_hash FROM admins WHERE username='admin'")["password_hash"].toString()));
        Database demo(dir->filePath("demo.db"));demo.migrate();demo.seedDemo();demo.seedDemo();
        QCOMPARE(demo.scalar("SELECT count(*) FROM users"),5);QCOMPARE(demo.scalar("SELECT count(*) FROM stations"),4);
        QCOMPARE(demo.scalar("SELECT count(*) FROM charging_piles"),12);QCOMPARE(demo.scalar("SELECT count(*) FROM charging_orders"),10);
        QCOMPARE(demo.scalar("SELECT count(*) FROM charging_orders WHERE status='UNPAID'"),1);
        QCOMPARE(demo.scalar("SELECT count(*) FROM admins WHERE username='admin'"),1);QVERIFY(demo.rows("PRAGMA foreign_key_check").isEmpty());
        Database showcase(dir->filePath("showcase.db"));showcase.migrate();showcase.execute("INSERT INTO stations(name,address,latitude,longitude,data_source,external_id,fast_connector_count,created_at,updated_at) VALUES('真实站','真实地址',39.9,116.4,'BEIJING_PUBLIC_DATA_OPEN_PLATFORM','1',4,?,?)",{utcNow(),utcNow()});showcase.seedShowcase();
        QCOMPARE(showcase.scalar("SELECT count(*) FROM users"),1);QCOMPARE(showcase.scalar("SELECT count(*) FROM stations"),1);QCOMPARE(showcase.scalar("SELECT count(*) FROM charging_piles"),1);QCOMPARE(showcase.scalar("SELECT count(*) FROM charging_orders"),0);
    }
    void catalogImportPreservesIds(){
        db->execute("INSERT INTO stations(id,name,address,latitude,longitude,data_source,external_id,created_at,updated_at) VALUES(7,'真实站','真实地址',39.9,116.4,'PUBLIC','EXT-7',?,?)",{utcNow(),utcNow()});
        const auto catalogPath=dir->filePath("catalog.json");QFile catalog(catalogPath);QVERIFY(catalog.open(QIODevice::WriteOnly));
        auto station=QJsonObject{{"name","真实站"},{"address","真实地址"},{"latitude",39.9},{"longitude",116.4},{"data_source","PUBLIC"},{"external_id","EXT-7"},{"service_type","社会公用"},{"region_scope","五环内"},{"location_type","停车场"},{"fast_connector_count",6},{"slow_connector_count",2}};
        auto bytes=QJsonDocument(QJsonObject{{"stations",QJsonArray{station}}}).toJson(QJsonDocument::Compact);QCOMPARE(catalog.write(bytes),bytes.size());catalog.close();
        Database target(dir->filePath("catalog.db"));target.migrate();target.importCatalog(catalogPath,db->path());
        QCOMPARE(target.scalar("SELECT id FROM stations WHERE data_source='PUBLIC' AND external_id='EXT-7'"),7);QCOMPARE(target.scalar("SELECT count(*) FROM stations"),1);
        QCOMPARE(target.scalar("SELECT fast_connector_count FROM stations WHERE id=7"),6);
        target.importCatalog(catalogPath,db->path());QCOMPARE(target.scalar("SELECT count(*) FROM stations"),1);
        station["external_id"]="UNMAPPED";QVERIFY(catalog.open(QIODevice::WriteOnly|QIODevice::Truncate));catalog.write(QJsonDocument(QJsonObject{{"stations",QJsonArray{station}}}).toJson(QJsonDocument::Compact));catalog.close();
        QVERIFY_EXCEPTION_THROWN(target.importCatalog(catalogPath,db->path()),Failure);QCOMPARE(target.scalar("SELECT count(*) FROM stations"),1);
    }
};
QTEST_GUILESS_MAIN(Tests)
#include "test_server.moc"
