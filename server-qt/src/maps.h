// 声明腾讯地图请求、路线/距离矩阵/静态图处理及缓存接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#pragma once
#include "common.h"
#include <QtNetwork>

// 持有地图 API 配置、网络管理器和短期响应缓存。
class Maps:public QObject {
public:
// 持有地图 API 配置、网络管理器和短期响应缓存。
    explicit Maps(QString key,QObject *parent=nullptr,QUrl base=QUrl("https://apis.map.qq.com"),int requestTimeout=5000,int totalTimeout=10000);
// 根据 action 调用地图服务，完成参数处理、结果转换和缓存命中/写入。
    void run(const QString &action,const QJsonObject &data,const QJsonArray &stations,Result done);
private:
// 实现 CacheEntry 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    struct CacheEntry { qint64 expiresAtMs=0; QJsonValue data; };
    QNetworkAccessManager network;
    QHash<QString,CacheEntry> cache;
    QString key;
    QUrl base;
    int requestTimeout,totalTimeout;
};
