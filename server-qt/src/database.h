#pragma once
#include "common.h"
#include <QtSql>

class Database {
public:
    explicit Database(const QString &path);
    ~Database();
    Database(const Database&)=delete;
    Database& operator=(const Database&)=delete;
    QSqlQuery query(const QString &sql,const QVariantList &args={});
    QJsonArray rows(const QString &sql,const QVariantList &args={});
    QJsonObject one(const QString &sql,const QVariantList &args={});
    qint64 scalar(const QString &sql,const QVariantList &args={});
    qint64 execute(const QString &sql,const QVariantList &args={});
    qint64 insert(const QString &sql,const QVariantList &args={});
    void migrate();
    void importLegacy(const QString &path);
    void importCatalog(const QString &path,const QString &idSource={});
    void createAdmin(const QString &name,const QString &password);
    void seedDemo();
    void seedShowcase();
    QString path() const { return db.databaseName(); }
private:
    QSqlDatabase db;
};
class Transaction {
public:
    explicit Transaction(Database &d): db(d) { db.execute("BEGIN IMMEDIATE"); }
    ~Transaction() { if(!done) { try { db.execute("ROLLBACK"); } catch(...) { qWarning("Database rollback failed"); } } }
    void commit() { db.execute("COMMIT"); done=true; }
    Transaction(const Transaction&)=delete;
private: Database &db; bool done=false;
};
