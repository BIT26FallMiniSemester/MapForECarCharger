#ifndef MODELS_H
#define MODELS_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QtMath>

// 与 docs/api.md 中的 JSON 字段一致。金额在接口里是「分」，界面展示时再换成「元」。

inline QString centsToYuanText(qint64 cents)
{
    return QString::number(static_cast<double>(cents) / 100.0, 'f', 2);
}

inline qint64 yuanToCents(double yuan)
{
    return static_cast<qint64>(qRound(yuan * 100.0));
}

inline double geoDistanceKm(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double kEarthKm = 6371.0;
    constexpr double kPi = 3.14159265358979323846;
    const auto rad = [](double deg) { return deg * kPi / 180.0; };
    const double dLat = rad(lat2 - lat1);
    const double dLon = rad(lon2 - lon1);
    const double a = qSin(dLat / 2) * qSin(dLat / 2)
                     + qCos(rad(lat1)) * qCos(rad(lat2)) * qSin(dLon / 2) * qSin(dLon / 2);
    return 2 * kEarthKm * qAsin(qSqrt(a));
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
    int priceCentsPerKwh = -1;  // 分/千瓦时；-1 表示未配置
    QString status;             // ACTIVE / INACTIVE
    QString operatorName;
    QString district;
    QString regionScope;
    QString locationType;
    bool hasCoordinates = false;
    bool isBookable = false;
    int totalPiles = 0;
    int availablePiles = 0;
    double onlineRate = 0;
    double distanceKm = -1;     // 附近查询时由后端计算；-1 表示未测距
};

struct StationFilterOptions {
    QStringList operatorNames;
    QStringList districts;
    QStringList regionScopes;
    QStringList locationTypes;
    int stationCount = 0;
    int withCoordinatesCount = 0;
    int withoutCoordinatesCount = 0;
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

inline QString energyText(qint64 energyWh)
{
    return QString::number(static_cast<double>(energyWh) / 1000.0, 'f', 3) + QStringLiteral(" kWh");
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
    qint64 userId = 0;
    qint64 stationId = 0;
    QString stationName;
    qint64 pileId = 0;
    QString pileNo;
    QString status;             // PENDING / RESERVED / CHARGING / UNPAID / COMPLETED / CANCELLED
    int unitPriceCentsPerKwh = 0;
    qint64 energyWh = 0;
    int durationSeconds = 0;
    qint64 amountCents = 0;
    QString reservedAt;
    QString reservationExpiresAt;
    QString startedAt;
    QString stoppedAt;
    QString settledAt;
    QString cancelledAt;
    QString cancelReason;
    QString createdAt;
    QString updatedAt;
    int ratedPowerW = 0;        // 客户端辅助字段，接口不一定返回
};

inline bool isActiveOrderStatus(const QString &status)
{
    return status == QLatin1String("PENDING")
           || status == QLatin1String("RESERVED")
           || status == QLatin1String("CHARGING")
           || status == QLatin1String("UNPAID");
}

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

inline QString formatIsoUtc(const QString &iso)
{
    if (iso.trimmed().isEmpty())
        return QStringLiteral("—");
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid())
        dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid())
        return iso;
    return dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

inline QString formatDurationSeconds(int seconds)
{
    if (seconds < 0)
        seconds = 0;
    const int h = seconds / 3600;
    const int m = (seconds % 3600) / 60;
    const int s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1小时%2分%3秒").arg(h).arg(m).arg(s);
    if (m > 0)
        return QStringLiteral("%1分%2秒").arg(m).arg(s);
    return QStringLiteral("%1秒").arg(s);
}

inline qint64 estimateEnergyWh(int ratedPowerW, int durationSeconds)
{
    if (ratedPowerW <= 0 || durationSeconds <= 0)
        return 0;
    return static_cast<qint64>(qRound(static_cast<double>(ratedPowerW) * durationSeconds / 3600.0));
}

inline qint64 estimateAmountCents(qint64 energyWh, int priceCentsPerKwh)
{
    if (energyWh <= 0 || priceCentsPerKwh <= 0)
        return 0;
    return qRound(static_cast<double>(energyWh) / 1000.0 * priceCentsPerKwh);
}

#endif
