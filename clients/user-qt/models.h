#ifndef MODELS_H
#define MODELS_H

#include <QString>
#include <QtGlobal>

// 与 docs/api.md 中的 JSON 字段一致。金额在接口里是「分」，界面展示时再换成「元」。

inline QString centsToYuanText(qint64 cents)
{
    return QString::number(static_cast<double>(cents) / 100.0, 'f', 2);
}

inline qint64 yuanToCents(double yuan)
{
    return static_cast<qint64>(qRound(yuan * 100.0));
}

struct User {
    qint64 id = 0;
    QString phone;
    QString nickname;
    QString avatarUrl;   // 为空时界面显示默认灰色头像
    qint64 balanceCents = 0;
    QString status;      // NORMAL / FROZEN
};

struct StationSummary {
    qint64 id = 0;
    QString name;
    QString address;
    QString operatorName;
    QString district;
    double latitude = 0;
    double longitude = 0;
    int priceCentsPerKwh = 0;   // 分/千瓦时，125 表示 1.25 元/度
    QString status;             // ACTIVE / INACTIVE
    int totalPiles = 0;
    int availablePiles = 0;
    int fastConnectorCount = 0;
    int slowConnectorCount = 0;
    double onlineRate = 0;
    double distanceKm = 0;      // 附近查询时由后端计算
};

struct ChargingPile {
    qint64 id = 0;
    qint64 stationId = 0;
    QString pileNo;
    QString chargeType;
    qint64 ratedPowerW = 0;
    QString status;
};

struct ChargingOrder {
    qint64 id = 0;
    QString orderNo;
    QString status;
    qint64 stationId = 0;
    QString stationName;
    qint64 pileId = 0;
    QString pileNo;
    qint64 ratedPowerW = 0;
    qint64 priceCentsPerKwh = 0;
    qint64 durationSeconds = 0;
    qint64 energyWh = 0;
    qint64 amountCents = 0;
    QString expiresAt;
    bool estimated = false;
};

struct RouteInfo {
    qint64 distanceMeters = 0;
    qint64 durationSeconds = 0;
};

struct RechargeRecord {
    QString rechargeNo;
    qint64 amountCents = 0;
    qint64 balanceAfterCents = 0;
    QString status;
    QString createdAt;
};

#endif
