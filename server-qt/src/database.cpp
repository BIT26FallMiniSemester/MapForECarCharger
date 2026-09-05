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
    if(!q.prepare(sql)) fail(50001);
    for(const auto &a:args) q.addBindValue(a);
    if(!q.exec()) fail(50001);
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
    if(scalar("SELECT count(*) FROM schema_migrations")!=1 || scalar("SELECT max(version) FROM schema_migrations")!=1) fail(50001);
    for(const auto &table:QStringList{"users","admins","stations","charging_piles","charging_orders","recharge_records","pile_status_logs","operation_logs"})
        if(!db.tables().contains(table)) fail(50001);
    if(!rows("PRAGMA foreign_key_check").isEmpty()) fail(50001);
}
void Database::createAdmin(const QString &name,const QString &password) {
    if(name.trimmed().isEmpty() || name.size()>32 || password.size()<8 || password.size()>128) fail(40001);
    if(scalar("SELECT count(*) FROM admins WHERE username=?",{name})) return;
    execute("INSERT INTO admins(username,password_hash,display_name,created_at,updated_at) VALUES(?,?,?,?,?)",{name,passwordHash(password),name,utcNow(),utcNow()});
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
        QVariantList values{s["name"].toString(),s["address"].toString(),s["latitude"].toDouble(),s["longitude"].toDouble(),s["operator_name"].toVariant(),s["district"].toVariant(),s["data_source"].toString(),s["external_id"].toString(),utcNow(),utcNow()};
        if(idSource.isEmpty()) {
            execute("INSERT INTO stations(name,address,latitude,longitude,operator_name,district,data_source,external_id,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?) ON CONFLICT(data_source,external_id) DO UPDATE SET name=excluded.name,address=excluded.address,latitude=excluded.latitude,longitude=excluded.longitude,operator_name=excluded.operator_name,district=excluded.district,updated_at=excluded.updated_at",values);
        } else {
            if(!preservedIds.contains(key)) fail(40001);
            const auto preserved=preservedIds[key];
            const auto existing=scalar("SELECT id FROM stations WHERE data_source=? AND external_id=?",{s["data_source"].toString(),s["external_id"].toString()});
            if(existing&&existing!=preserved) fail(40001);
            values.prepend(preserved);
            execute("INSERT INTO stations(id,name,address,latitude,longitude,operator_name,district,data_source,external_id,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(data_source,external_id) DO UPDATE SET name=excluded.name,address=excluded.address,latitude=excluded.latitude,longitude=excluded.longitude,operator_name=excluded.operator_name,district=excluded.district,updated_at=excluded.updated_at",values);
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
