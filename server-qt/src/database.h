// 声明 SQLite 数据库连接、查询结果转换、迁移、演示数据和数据导入接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#pragma once
#include "common.h"
#include <QtSql>

// 封装数据库构造、析构和 SQLite 生命周期管理。
class Database {
public:
// 封装数据库构造、析构和 SQLite 生命周期管理。
    explicit Database(const QString &path);
// 封装数据库构造、析构和 SQLite 生命周期管理。
    ~Database();
    Database(const Database&)=delete;
    Database& operator=(const Database&)=delete;
    QSqlQuery query(const QString &sql,const QVariantList &args={});
    QJsonArray rows(const QString &sql,const QVariantList &args={});
    QJsonObject one(const QString &sql,const QVariantList &args={});
    qint64 scalar(const QString &sql,const QVariantList &args={});
    qint64 execute(const QString &sql,const QVariantList &args={});
    qint64 insert(const QString &sql,const QVariantList &args={});
// 按 schema_migrations 顺序应用数据库迁移并检查关键表和外键。
    void migrate();
    void importCatalog(const QString &path,const QString &idSource={});
// 以幂等方式创建管理员，校验账号长度并保存加密密码摘要。
    void createAdmin(const QString &name,const QString &password);
// 在空数据库中写入一组可重复使用的 Qt 演示数据。
    void seedDemo();
// 在已导入的北京公共目录上创建测试用户和受管测试电桩。
    void seedShowcase();
// 实现 path 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    QString path() const { return db.databaseName(); }
private:
    QSqlDatabase db;
};
// 用 RAII 管理 SQLite 事务，析构时自动回滚未提交操作。
class Transaction {
public:
// 用 RAII 管理 SQLite 事务，析构时自动回滚未提交操作。
    explicit Transaction(Database &d): db(d) { db.execute("BEGIN IMMEDIATE"); }
// 用 RAII 管理 SQLite 事务，析构时自动回滚未提交操作。
    ~Transaction() { if(!done) { try { db.execute("ROLLBACK"); } catch(...) { qWarning("Database rollback failed"); } } }
// 提交当前事务并标记已提交，避免析构阶段重复回滚。
    void commit() { db.execute("COMMIT"); done=true; }
    Transaction(const Transaction&)=delete;
private: Database &db; bool done=false;
};
