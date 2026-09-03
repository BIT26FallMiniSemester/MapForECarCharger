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
    double latitude = 0;
    double longitude = 0;
    int priceCentsPerKwh = 0;   // 分/千瓦时，125 表示 1.25 元/度
    QString status;             // ACTIVE / INACTIVE
    int totalPiles = 0;
    int availablePiles = 0;
    double onlineRate = 0;
    double distanceKm = 0;      // 附近查询时由后端计算
};

struct ChargingPile {
    qint64 id = 0;
    QString pileNo;             // 全局唯一编号，如 BJ-H-0012
    qint64 stationId = 0;
    QString stationName;
    QString pileType;           // FAST / SLOW
    int ratedPowerW = 0;        // 额定功率，瓦
    QString status;             // IDLE / RESERVED / CHARGING / FAULT / OFFLINE
    QString lastHeartbeatAt;
    int totalChargeCount = 0;
    int totalChargeDurationSeconds = 0;
};

inline QString pileTypeText(const QString &pileType)
{
    if (pileType == QLatin1String("FAST"))
        return QStringLiteral("快充");
    if (pileType == QLatin1String("SLOW"))
        return QStringLiteral("慢充");
    return pileType.isEmpty() ? QStringLiteral("未知") : pileType;
}

inline QString pileStatusText(const QString &status)
{
    if (status == QLatin1String("IDLE"))
        return QStringLiteral("空闲");
    if (status == QLatin1String("RESERVED"))
        return QStringLiteral("已预约");
    if (status == QLatin1String("CHARGING"))
        return QStringLiteral("充电中");
    if (status == QLatin1String("FAULT"))
        return QStringLiteral("故障");
    if (status == QLatin1String("OFFLINE"))
        return QStringLiteral("离线");
    return status.isEmpty() ? QStringLiteral("未知") : status;
}

inline QString powerText(int watts)
{
    if (watts <= 0)
        return QStringLiteral("未知");
    if (watts % 1000 == 0)
        return QStringLiteral("%1 kW").arg(watts / 1000);
    return QStringLiteral("%1 W").arg(watts);
}

struct RechargeRecord {
    QString rechargeNo;
    qint64 amountCents = 0;
    qint64 balanceAfterCents = 0;
    QString status;
    QString createdAt;
};

struct ChargingOrder {
    qint64 id = 0;
    QString orderNo;
    qint64 stationId = 0;
    QString stationName;
    qint64 pileId = 0;
    QString pileNo;
    QString status;             // PENDING / RESERVED / CHARGING / UNPAID / COMPLETED / CANCELLED
    int unitPriceCentsPerKwh = 0;
    qint64 energyWh = 0;
    int durationSeconds = 0;
    qint64 amountCents = 0;
    QString createdAt;
};

inline QString orderStatusText(const QString &status)
{
    if (status == QLatin1String("PENDING"))
        return QStringLiteral("待预约");
    if (status == QLatin1String("RESERVED"))
        return QStringLiteral("已预约");
    if (status == QLatin1String("CHARGING"))
        return QStringLiteral("充电中");
    if (status == QLatin1String("UNPAID"))
        return QStringLiteral("待结算");
    if (status == QLatin1String("COMPLETED"))
        return QStringLiteral("已完成");
    if (status == QLatin1String("CANCELLED"))
        return QStringLiteral("已取消");
    return status.isEmpty() ? QStringLiteral("未知") : status;
}

#endif
