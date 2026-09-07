#include "database.h"

Database::Database(const QString &path) {
    if(path!=":memory:") QDir().mkpath(QFileInfo(path).absolutePath());
    db=QSqlDatabase::addDatabase("QSQLITE",QUuid::createUuid().toString());
    db.setDatabaseName(path);
    if(!db.open()) fail(50001);
    execute("PRAGMA foreign_keys=ON"); execute("PRAGMA journal_mode=WAL"); execute("PRAGMA busy_timeout=10000");
}
Database::~Database() { const auto name=db.connectionName(); db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
QSqlQuery Database::query(const QString &sql,const QVariantList &args) {
    QSqlQuery q(db);
    if(!q.prepare(sql)) { qWarning().noquote()<<"Database prepare failed:"<<q.lastError().text(); fail(50001); }
    for(const auto &a:args) q.addBindValue(a);
    if(!q.exec()) { qWarning().noquote()<<"Database query failed:"<<q.lastError().text(); fail(50001); }
    return q;
}
QJsonArray Database::rows(const QString &sql,const QVariantList &args) {
    auto q=query(sql,args); QJsonArray out;
    while(q.next()) {
        QJsonObject row; const auto record=q.record();
        for(int i=0;i<record.count();++i) row[record.fieldName(i)]=q.isNull(i)?QJsonValue(QJsonValue::Null):QJsonValue::fromVariant(q.value(i));
        out.append(row);
    } return out;
}
QJsonObject Database::one(const QString &sql,const QVariantList &args) { const auto all=rows(sql,args); return all.isEmpty()?QJsonObject():all[0].toObject(); }
qint64 Database::scalar(const QString &sql,const QVariantList &args) { auto q=query(sql,args); return q.next()?q.value(0).toLongLong():0; }
qint64 Database::execute(const QString &sql,const QVariantList &args) { auto q=query(sql,args); return q.numRowsAffected(); }
qint64 Database::insert(const QString &sql,const QVariantList &args) { auto q=query(sql,args); return q.lastInsertId().toLongLong(); }
void Database::migrate() {
    if(!db.tables().contains("schema_migrations")) {
        if(!db.tables().isEmpty()) fail(50001);
        Transaction tx(*this);
        execute("CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY,applied_at TEXT NOT NULL)");
        QFile file(":/migrations/001_initial.sql"); if(!file.open(QIODevice::ReadOnly)) fail(50000);
        for(const auto &sql:QString::fromUtf8(file.readAll()).split(';',Qt::SkipEmptyParts)) if(!sql.trimmed().isEmpty()) execute(sql);
        execute("INSERT INTO schema_migrations VALUES(1,?)",{utcNow()}); tx.commit();
    }
    const auto version=scalar("SELECT max(version) FROM schema_migrations");
    if(version<1||version>2||scalar("SELECT count(*) FROM schema_migrations")!=version) fail(50001);
    if(version==1) {
        Transaction tx(*this);QFile file(":/migrations/002_catalog_details.sql");if(!file.open(QIODevice::ReadOnly))fail(50000);
        for(const auto &sql:QString::fromUtf8(file.readAll()).split(';',Qt::SkipEmptyParts))if(!sql.trimmed().isEmpty())execute(sql);
        execute("INSERT INTO schema_migrations VALUES(2,?)",{utcNow()});tx.commit();
    }
    for(const auto &table:QStringList{"users","admins","stations","charging_piles","charging_orders","recharge_records","pile_status_logs","operation_logs"})
        if(!db.tables().contains(table)) fail(50001);
    if(!rows("PRAGMA foreign_key_check").isEmpty()) fail(50001);
}
void Database::createAdmin(const QString &name,const QString &password) {
    if(name.trimmed().isEmpty() || name.size()>32 || password.size()<8 || password.size()>128) fail(40001);
    if(scalar("SELECT count(*) FROM admins WHERE username=?",{name})) return;
    execute("INSERT INTO admins(username,password_hash,display_name,created_at,updated_at) VALUES(?,?,?,?,?)",{name,passwordHash(password),name,utcNow(),utcNow()});
}
void Database::seedDemo() {
    if(scalar("SELECT count(*) FROM stations WHERE data_source='DEMO'")) return;
    for(const auto &table:QStringList{"users","stations","charging_piles","charging_orders","recharge_records"})
        if(scalar("SELECT count(*) FROM "+table)) fail(40002);
    createAdmin("admin","admin123");
    const auto now=QDateTime::currentDateTimeUtc();
    auto stamp=[&](int days,int minutes=0){return now.addDays(days).addSecs(minutes*60).toString(Qt::ISODateWithMs);};
    Transaction tx(*this);
    execute("INSERT INTO users(id,phone,nickname,balance_cents,status,created_at,updated_at) VALUES"
            "(1,'13900000000','演示车主',24810,'NORMAL',?,?),"
            "(2,'13800000000','冻结测试用户',5000,'FROZEN',?,?),"
            "(3,'13700000000','预约中用户',12000,'NORMAL',?,?),"
            "(4,'13600000000','充电中用户',18000,'NORMAL',?,?),"
            "(5,'13500000000','待支付用户',6000,'NORMAL',?,?)",
            {stamp(-30),stamp(0),stamp(-20),stamp(0),stamp(-10),stamp(0),stamp(-8),stamp(0),stamp(-6),stamp(0)});
    execute("INSERT INTO stations(id,name,address,latitude,longitude,price_cents_per_kwh,operator_name,district,data_source,external_id,status,created_at,updated_at) VALUES"
            "(1,'王府井绿色充电站','北京市东城区王府井大街88号',39.9142,116.4108,148,'京能充电','东城区','DEMO','BJ-DONG-001','ACTIVE',?,?),"
            "(2,'金融街超级充电站','北京市西城区金融大街15号',39.9148,116.3612,172,'国家电网','西城区','DEMO','BJ-XICHENG-001','ACTIVE',?,?),"
            "(3,'国贸智慧充电中心','北京市朝阳区建国门外大街1号',39.9087,116.4590,165,'特来电','朝阳区','DEMO','BJ-CHAOYANG-001','ACTIVE',?,?),"
            "(4,'中关村低碳充电站','北京市海淀区中关村大街27号',39.9830,116.3157,136,'星星充电','海淀区','DEMO','BJ-HAIDIAN-001','ACTIVE',?,?)",
            {stamp(-60),stamp(0),stamp(-55),stamp(0),stamp(-45),stamp(0),stamp(-40),stamp(0)});
    execute("INSERT INTO charging_piles(id,station_id,pile_no,charge_type,rated_power_w,status,created_at,updated_at) VALUES"
            "(1,1,'BJ-WFJ-F01','FAST',120000,'IDLE',?,?),"
            "(2,1,'BJ-WFJ-F02','FAST',120000,'IDLE',?,?),"
            "(3,1,'BJ-WFJ-S01','SLOW',7000,'FAULT',?,?),"
            "(4,2,'BJ-JRJ-F01','FAST',160000,'IDLE',?,?),"
            "(5,2,'BJ-JRJ-F02','FAST',160000,'RESERVED',?,?),"
            "(6,3,'BJ-GM-F01','FAST',180000,'CHARGING',?,?),"
            "(7,3,'BJ-GM-F02','FAST',180000,'IDLE',?,?),"
            "(8,3,'BJ-GM-S01','SLOW',7000,'OFFLINE',?,?),"
            "(9,4,'BJ-ZGC-F01','FAST',120000,'IDLE',?,?),"
            "(10,4,'BJ-ZGC-F02','FAST',120000,'IDLE',?,?),"
            "(11,4,'BJ-ZGC-S01','SLOW',7000,'IDLE',?,?),"
            "(12,4,'BJ-ZGC-S02','SLOW',7000,'IDLE',?,?)",
            {stamp(-60),stamp(0),stamp(-60),stamp(0),stamp(-60),stamp(-1),stamp(-55),stamp(0),stamp(-55),stamp(0),stamp(-45),stamp(0),stamp(-45),stamp(0),stamp(-45),stamp(-2),stamp(-40),stamp(0),stamp(-40),stamp(0),stamp(-40),stamp(0),stamp(-40),stamp(0)});
    const QList<int> amounts{1250,1680,980,2300,1420,1890,670};
    const QList<int> energy{8500,11000,6400,15000,9200,12300,4500};
    for(int i=0;i<amounts.size();++i) {
        const int days=i==0?0:-i;
        const int stationId=i%4+1;
        const int pileId=QList<int>{1,4,7,9}[i%4];
        execute("INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,started_at,stopped_at,duration_seconds,energy_wh,amount_cents,paid_at,created_at,updated_at) VALUES(?,?,?,?,?,'COMPLETED',?,?,?,?,?,?,?,?,?)",
                {i+1,QString("DEMO-C%1").arg(i+1,4,10,QChar('0')),1,stationId,pileId,
                 stationId==1?148:stationId==2?172:stationId==3?165:136,
                 stamp(days,-70),stamp(days,-10),3600,energy[i],amounts[i],stamp(days,-8),stamp(days,-75),stamp(days,-8)});
    }
    execute("INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,reserved_at,expires_at,created_at,updated_at) VALUES(8,'DEMO-R0001',3,2,5,'RESERVED',172,?,?,?,?)",
            {stamp(0,-2),stamp(0,13),stamp(0,-3),stamp(0,-2)});
    execute("INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,reserved_at,expires_at,started_at,created_at,updated_at) VALUES(9,'DEMO-CURRENT',4,3,6,'CHARGING',165,?,?,?,?,?)",
            {stamp(0,-32),stamp(0,-17),stamp(0,-30),stamp(0,-33),stamp(0,-30)});
    execute("INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,started_at,stopped_at,duration_seconds,energy_wh,amount_cents,created_at,updated_at) VALUES(10,'DEMO-UNPAID',5,4,10,'UNPAID',136,?,?,?,?,?,?,?)",
            {stamp(-1,-80),stamp(-1,-20),3600,9200,1251,stamp(-1,-85),stamp(-1,-20)});
    execute("UPDATE charging_piles SET reserved_order_id=8 WHERE id=5");
    execute("UPDATE charging_piles SET reserved_order_id=9 WHERE id=6");
    execute("INSERT INTO recharge_records(id,user_id,client_request_id,amount_cents,balance_after_cents,created_at) VALUES"
            "(1,1,'demo-recharge-001',10000,10000,?),(2,1,'demo-recharge-002',25000,35000,?)",
            {stamp(-25),stamp(-12)});
    execute("INSERT INTO pile_status_logs(pile_id,order_id,old_status,new_status,reason,created_at) VALUES"
            "(3,NULL,'IDLE','FAULT','DEMO_FAULT',?),(8,NULL,'IDLE','OFFLINE','DEMO_OFFLINE',?),"
            "(5,8,'IDLE','RESERVED','RESERVE',?),(6,9,'IDLE','RESERVED','RESERVE',?),(6,9,'RESERVED','CHARGING','START',?)",
            {stamp(-1),stamp(-2),stamp(0,-2),stamp(0,-32),stamp(0,-30)});
    tx.commit();
}
void Database::seedShowcase() {
    if(!scalar("SELECT count(*) FROM stations WHERE data_source='BEIJING_PUBLIC_DATA_OPEN_PLATFORM'"))fail(40001);
    if(scalar("SELECT count(*) FROM stations WHERE data_source='DEMO'")||scalar("SELECT count(*) FROM users")||scalar("SELECT count(*) FROM charging_piles")||scalar("SELECT count(*) FROM charging_orders"))fail(40002);
    createAdmin("admin","admin123");
    Transaction tx(*this);const auto now=utcNow();
    execute("INSERT INTO users(phone,nickname,balance_cents,status,created_at,updated_at) VALUES('13900000000','测试车主',30000,'NORMAL',?,?)",{now,now});
    execute("UPDATE stations SET price_cents_per_kwh=150,updated_at=? WHERE data_source='BEIJING_PUBLIC_DATA_OPEN_PLATFORM'",{now});
    const auto stations=rows("SELECT id,max(fast_connector_count,0) fast_count,max(slow_connector_count,0) slow_count FROM stations WHERE data_source='BEIJING_PUBLIC_DATA_OPEN_PLATFORM' ORDER BY id");
    for(const auto &value:stations) {
        const auto station=value.toObject();
        const auto stationId=station["id"].toInteger();
        const int fast=station["fast_count"].toInt(),slow=station["slow_count"].toInt();
        for(int i=1;i<=fast;++i)
            execute("INSERT INTO charging_piles(station_id,pile_no,charge_type,rated_power_w,status,created_at,updated_at) VALUES(?,?,?,?, 'IDLE',?,?)",
                    {stationId,QString("BJ-%1-F%2").arg(stationId).arg(i,3,10,QChar('0')),"FAST",120000,now,now});
        for(int i=1;i<=slow;++i)
            execute("INSERT INTO charging_piles(station_id,pile_no,charge_type,rated_power_w,status,created_at,updated_at) VALUES(?,?,?,?, 'IDLE',?,?)",
                    {stationId,QString("BJ-%1-S%2").arg(stationId).arg(i,3,10,QChar('0')),"SLOW",7000,now,now});
        if(fast+slow==0)
            execute("INSERT INTO charging_piles(station_id,pile_no,charge_type,rated_power_w,status,created_at,updated_at) VALUES(?,?, 'FAST',120000,'IDLE',?,?)",
                    {stationId,QString("BJ-%1-T001").arg(stationId),now,now});
    }
    tx.commit();
}
void Database::importCatalog(const QString &path,const QString &idSource) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) fail(40001);
    const auto doc=QJsonDocument::fromJson(file.readAll());
    if(!doc.isObject() || !doc.object()["stations"].isArray()) fail(40001);
    QHash<QString,qint64> preservedIds;
    QString idConnection;
    if(!idSource.isEmpty()) {
        if(QFileInfo(idSource).canonicalFilePath()==QFileInfo(db.databaseName()).canonicalFilePath()) fail(40001);
        idConnection=QUuid::createUuid().toString();
        {
            auto source=QSqlDatabase::addDatabase("QSQLITE",idConnection);
            source.setConnectOptions("QSQLITE_OPEN_READONLY");source.setDatabaseName(idSource);
            if(!source.open()) fail(40001);
            QSqlQuery q(source);
            if(!q.exec("SELECT id,data_source,external_id FROM stations WHERE data_source IS NOT NULL AND external_id IS NOT NULL")) fail(40001);
            while(q.next()) {
                const auto key=q.value(1).toString()+QChar(0x1f)+q.value(2).toString();
                const qint64 id=q.value(0).toLongLong();
                if(id<=0||preservedIds.contains(key)) fail(40001);
                preservedIds.insert(key,id);
            }
            source.close();
        }
        QSqlDatabase::removeDatabase(idConnection);
        if(preservedIds.isEmpty()) fail(40001);
    }
    Transaction tx(*this);
    for(auto item:doc.object()["stations"].toArray()) {
        auto s=item.toObject();
        if(s["data_source"].toString().isEmpty()||s["external_id"].toString().isEmpty()
           ||s["name"].toString().trimmed().isEmpty()||s["address"].toString().trimmed().isEmpty()
           ||!s["latitude"].isDouble()||!s["longitude"].isDouble()) fail(40001);
        const auto key=s["data_source"].toString()+QChar(0x1f)+s["external_id"].toString();
        QVariantList values{s["name"].toString(),s["address"].toString(),s["latitude"].toDouble(),s["longitude"].toDouble(),s["operator_name"].toVariant(),s["district"].toVariant(),s["data_source"].toString(),s["external_id"].toString(),s["service_type"].toVariant(),s["region_scope"].toVariant(),s["location_type"].toVariant(),s["fast_connector_count"].toInt(),s["slow_connector_count"].toInt(),utcNow(),utcNow()};
        if(idSource.isEmpty()) {
            execute("INSERT INTO stations(name,address,latitude,longitude,operator_name,district,data_source,external_id,service_type,region_scope,location_type,fast_connector_count,slow_connector_count,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(data_source,external_id) DO UPDATE SET name=excluded.name,address=excluded.address,latitude=excluded.latitude,longitude=excluded.longitude,operator_name=excluded.operator_name,district=excluded.district,service_type=excluded.service_type,region_scope=excluded.region_scope,location_type=excluded.location_type,fast_connector_count=excluded.fast_connector_count,slow_connector_count=excluded.slow_connector_count,updated_at=excluded.updated_at",values);
        } else {
            if(!preservedIds.contains(key)) fail(40001);
            const auto preserved=preservedIds[key];
            const auto existing=scalar("SELECT id FROM stations WHERE data_source=? AND external_id=?",{s["data_source"].toString(),s["external_id"].toString()});
            if(existing&&existing!=preserved) fail(40001);
            values.prepend(preserved);
            execute("INSERT INTO stations(id,name,address,latitude,longitude,operator_name,district,data_source,external_id,service_type,region_scope,location_type,fast_connector_count,slow_connector_count,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(data_source,external_id) DO UPDATE SET name=excluded.name,address=excluded.address,latitude=excluded.latitude,longitude=excluded.longitude,operator_name=excluded.operator_name,district=excluded.district,service_type=excluded.service_type,region_scope=excluded.region_scope,location_type=excluded.location_type,fast_connector_count=excluded.fast_connector_count,slow_connector_count=excluded.slow_connector_count,updated_at=excluded.updated_at",values);
        }
    } tx.commit();
}
void Database::importLegacy(const QString &path) {
    if(QFileInfo(path).canonicalFilePath()==QFileInfo(db.databaseName()).canonicalFilePath()) fail(40001);
    const QStringList tables{"users","admins","stations","charging_piles","charging_orders","recharge_records","pile_status_logs","operation_logs"};
    for(const auto &t:tables) if(scalar("SELECT count(*) FROM "+t)) fail(40001);
    const QString sourceName=QUuid::createUuid().toString();
    {
        auto source=QSqlDatabase::addDatabase("QSQLITE",sourceName);
        source.setConnectOptions("QSQLITE_OPEN_READONLY"); source.setDatabaseName(path);
        if(!source.open()) fail(40001);
        auto sourceQuery=[&](const QString &sql) { QSqlQuery q(source); if(!q.exec(sql)) fail(40001); return q; };
        auto version=sourceQuery("SELECT version_num FROM alembic_version");
        if(!version.next()||version.value(0).toString()!="c9f7e2a4b6d1") fail(40001);
        Transaction tx(*this);
        const QMap<QString,QString> aliases{{"charge_type","pile_type"},{"price_cents_per_kwh","unit_price_cents_per_kwh"},{"expires_at","reservation_expires_at"},{"paid_at","settled_at"},{"detail_json","request_json"}};
        for(const auto &table:tables) {
            auto fields=rows("PRAGMA table_info("+table+")"); QStringList names,marks;
            for(auto f:fields) { names<<f.toObject()["name"].toString(); marks<<"?"; }
            auto q=sourceQuery("SELECT * FROM "+table);
            while(q.next()) {
                QVariantList values;
                for(const auto &name:names) {
                    QString old=name;
                    if((name!="price_cents_per_kwh"||table=="charging_orders")&&aliases.contains(name)) old=aliases[name];
                    const int index=q.record().indexOf(old); QVariant v=index<0?QVariant():q.value(index);
                    if(name=="avatar_id"||name=="reserved_order_id") v=QVariant();
                    if(!v.isNull()&&(name.endsWith("_at")||name=="expires_at")) {
                        QString text=v.toString().replace(' ','T');
                        if(!text.endsWith('Z')&&!text.contains(QRegularExpression("[+-]\\d\\d:\\d\\d$"))) text+='Z';
                        // Python stores microseconds; the Qt contract uses milliseconds.
                        text.replace(QRegularExpression("(\\.\\d{3})\\d+(Z)$"),"\\1\\2");
                        const auto date=QDateTime::fromString(text,Qt::ISODateWithMs);
                        if(!date.isValid()) fail(40001);
                        v=date.toUTC().toString(Qt::ISODateWithMs);
                    }
                    if(table=="pile_status_logs"&&name=="reason") {
                        const auto oldStatus=q.value("old_status").toString(), next=q.value("new_status").toString();
                        if(next=="RESERVED") v="RESERVE";
                        else if(next=="CHARGING") v="START";
                        else if(oldStatus=="CHARGING"&&next=="IDLE") v="STOP";
                        else if(oldStatus=="RESERVED"&&next=="IDLE") v=v.toString().contains("expired")?"EXPIRE":"CANCEL";
                        else if(oldStatus=="FAULT"&&next=="IDLE") v="ADMIN_RECOVER";
                        else if(v.isNull()) v="LEGACY";
                    }
                    values<<v;
                }
                execute("INSERT INTO "+table+"("+names.join(',')+") VALUES("+marks.join(',')+")",values);
            }
        }
        execute("UPDATE charging_piles SET reserved_order_id=(SELECT id FROM charging_orders WHERE pile_id=charging_piles.id AND status IN ('RESERVED','CHARGING'))");
        if(scalar("SELECT count(*) FROM charging_piles p LEFT JOIN charging_orders o ON o.id=p.reserved_order_id WHERE (p.status IN ('RESERVED','CHARGING') AND (o.id IS NULL OR o.status<>p.status)) OR (p.status NOT IN ('RESERVED','CHARGING') AND o.id IS NOT NULL)")) fail(40001);
        if(!rows("PRAGMA foreign_key_check").isEmpty()) fail(40001);
        tx.commit(); source.close();
    }
    QSqlDatabase::removeDatabase(sourceName);
}
