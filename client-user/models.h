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

struct RechargeRecord {
    QString rechargeNo;
    qint64 amountCents = 0;
    qint64 balanceAfterCents = 0;
    QString status;
    QString createdAt;
};

#endif
