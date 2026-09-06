#pragma once
#include "common.h"
#include <QtNetwork>

class Maps:public QObject {
public:
    explicit Maps(QString key,QObject *parent=nullptr,QUrl base=QUrl("https://apis.map.qq.com"),int requestTimeout=5000,int totalTimeout=10000);
    void run(const QString &action,const QJsonObject &data,const QJsonArray &stations,Result done);
private:
    struct CacheEntry { qint64 expiresAtMs=0; QJsonValue data; };
    QNetworkAccessManager network;
    QHash<QString,CacheEntry> cache;
    QString key;
    QUrl base;
    int requestTimeout,totalTimeout;
};
