#pragma once
#include "database.h"

struct Identity { qint64 id=0; QString role; QDateTime expires; };
class Business {
public:
    explicit Business(Database &database): db(database) {}
    Identity authorize(const QString &action,const QString &token);
    QJsonValue dispatch(const QString &action,const QJsonObject &data,const Identity &identity);
    void expire();
    QJsonObject station(qint64 id);
    QJsonArray nearbyCandidates();
private:
    Database &db;
    QHash<QString,Identity> sessions;
    QJsonObject user(qint64 id);
    QJsonObject pile(qint64 id);
    QJsonObject order(qint64 id);
    QJsonValue orderAction(const QString &action,const QJsonObject &data,qint64 userId);
    QJsonValue adminAction(const QString &action,const QJsonObject &data,qint64 adminId);
    void pileLog(qint64 pileId,qint64 orderId,const QString &before,const QString &after,const QString &reason);
    void operationLog(qint64 admin,const QString &action,const QString &type,qint64 target,const QJsonObject &data);
    QJsonObject listing(const QString &sql,const QVariantList &args,const QJsonObject &data,const std::function<QJsonObject(QJsonObject)> &transform={});
};
