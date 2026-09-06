#include "apiclient.h"

#include "socketclient.h"

#include <QJsonArray>
#include <QUuid>

ApiClient::ApiClient(QObject *parent) : QObject(parent), m_socket(new SocketClient(this))
{
    connect(m_socket, &SocketClient::succeeded, this,
            [this](const QString &context, const QJsonValue &data, const QJsonObject &) {
        emit requestFinished();
        handleSuccess(context, data);
    });
    connect(m_socket, &SocketClient::failed, this,
            [this](const QString &, int code, const QString &message) {
        emit requestFinished();
        emit apiFailed(code, chineseMessage(code, message));
    });
}

void ApiClient::setBaseUrl(const QString &endpoint) { m_socket->setEndpoint(endpoint); }
void ApiClient::setDemoMode(bool enabled) { Q_UNUSED(enabled); }
void ApiClient::setToken(const QString &token) { m_token = token; }
void ApiClient::clearSession() { m_token.clear(); }

void ApiClient::login(const QString &phone)
{
    send(QStringLiteral("login"), QStringLiteral("auth.user.login"),
         QJsonObject{{QStringLiteral("phone"), phone}}, false);
}

void ApiClient::fetchProfile()
{
    send(QStringLiteral("profile"), QStringLiteral("users.me.get"));
}

void ApiClient::updateNickname(const QString &nickname)
{
    send(QStringLiteral("nickname"), QStringLiteral("users.me.update"),
         QJsonObject{{QStringLiteral("nickname"), nickname.trimmed()}});
}

void ApiClient::recharge(double amountYuan)
{
    send(QStringLiteral("recharge"), QStringLiteral("wallet.recharges.create"),
         QJsonObject{{QStringLiteral("amount_cents"), yuanToCents(amountYuan)},
                     {QStringLiteral("client_request_id"), QStringLiteral("qt-")
                          + QUuid::createUuid().toString(QUuid::WithoutBraces)}});
}

void ApiClient::fetchRechargeRecords()
{
    send(QStringLiteral("rechargeRecords"), QStringLiteral("wallet.recharges.list"),
         QJsonObject{{QStringLiteral("page"), 1}, {QStringLiteral("page_size"), 20}});
}

void ApiClient::fetchNearbyStations(double latitude, double longitude, double radiusKm)
{
    send(QStringLiteral("nearby"), QStringLiteral("stations.nearby"),
         QJsonObject{{QStringLiteral("latitude"), latitude},
                     {QStringLiteral("longitude"), longitude},
                     {QStringLiteral("radius_km"), radiusKm},
                     {QStringLiteral("page"), 1},
                     {QStringLiteral("page_size"), 100}});
}

void ApiClient::send(const QString &context, const QString &action,
                     const QJsonObject &data, bool withToken)
{
    emit requestStarted();
    m_socket->send(context, action, data, withToken ? m_token : QString());
}

void ApiClient::handleSuccess(const QString &context, const QJsonValue &data)
{
    if (context == QStringLiteral("login")) {
        const QJsonObject object = data.toObject();
        const QString token = object.value(QStringLiteral("access_token")).toString();
        setToken(token);
        emit loginSucceeded(token,
                            parseUser(object.value(QStringLiteral("user")).toObject()),
                            object.value(QStringLiteral("is_new_user")).toBool());
        return;
    }
    if (context == QStringLiteral("profile") || context == QStringLiteral("nickname")) {
        const User user = parseUser(data.toObject());
        if (context == QStringLiteral("nickname"))
            emit nicknameUpdated(user);
        else
            emit profileReady(user);
        return;
    }
    if (context == QStringLiteral("recharge")) {
        const RechargeRecord record = parseRecharge(data.toObject());
        emit rechargeSucceeded(record.balanceAfterCents, record);
        return;
    }
    if (context == QStringLiteral("rechargeRecords")) {
        QVector<RechargeRecord> records;
        for (const QJsonValue &value : data.toObject().value(QStringLiteral("items")).toArray())
            records.push_back(parseRecharge(value.toObject()));
        emit rechargeRecordsReady(records);
        return;
    }
    if (context == QStringLiteral("nearby")) {
        QVector<StationSummary> stations;
        for (const QJsonValue &value : data.toObject().value(QStringLiteral("items")).toArray())
            stations.push_back(parseStation(value.toObject()));
        emit nearbyStationsReady(stations);
    }
}

User ApiClient::parseUser(const QJsonObject &object) const
{
    User user;
    user.id = object.value(QStringLiteral("id")).toInteger();
    user.phone = object.value(QStringLiteral("phone")).toString();
    user.nickname = object.value(QStringLiteral("nickname")).toString();
    user.avatarUrl = object.value(QStringLiteral("avatar_id")).toString();
    user.balanceCents = object.value(QStringLiteral("balance_cents")).toInteger();
    user.status = object.value(QStringLiteral("status")).toString();
    return user;
}

StationSummary ApiClient::parseStation(const QJsonObject &object) const
{
    StationSummary station;
    station.id = object.value(QStringLiteral("id")).toInteger();
    station.name = object.value(QStringLiteral("name")).toString();
    station.address = object.value(QStringLiteral("address")).toString();
    station.latitude = object.value(QStringLiteral("latitude")).toDouble();
    station.longitude = object.value(QStringLiteral("longitude")).toDouble();
    station.priceCentsPerKwh = object.value(QStringLiteral("price_cents_per_kwh")).toInt();
    station.status = object.value(QStringLiteral("status")).toString();
    station.totalPiles = object.value(QStringLiteral("total_piles")).toInt();
    station.availablePiles = object.value(QStringLiteral("available_piles")).toInt();
    station.onlineRate = station.totalPiles ? station.availablePiles * 100.0 / station.totalPiles : 0.0;
    station.distanceKm = object.value(QStringLiteral("route_distance_meters")).toDouble() / 1000.0;
    return station;
}

RechargeRecord ApiClient::parseRecharge(const QJsonObject &object) const
{
    RechargeRecord record;
    record.rechargeNo = QStringLiteral("RC%1").arg(object.value(QStringLiteral("record_id")).toInteger());
    record.amountCents = object.value(QStringLiteral("amount_cents")).toInteger();
    record.balanceAfterCents = object.value(QStringLiteral("balance_cents")).toInteger();
    record.status = QStringLiteral("SUCCESS");
    record.createdAt = object.value(QStringLiteral("created_at")).toString();
    return record;
}

QString ApiClient::chineseMessage(int code, const QString &fallback) const
{
    switch (code) {
    case 40001: return QStringLiteral("参数错误，请检查手机号或输入内容");
    case 40002: return QStringLiteral("当前已有未完成订单");
    case 40003: return QStringLiteral("站点或电桩当前不可用");
    case 40004: return QStringLiteral("订单状态不允许该操作");
    case 40006: return QStringLiteral("余额不足，请先充值");
    case 40101: return QStringLiteral("登录已过期，请重新登录");
    case 40301: return QStringLiteral("该账号已被冻结或无权操作");
    case 40401: return QStringLiteral("目标记录不存在");
    case 50301: return QStringLiteral("地图服务暂不可用");
    case 50001: return fallback;
    default: break;
    }
    return fallback.isEmpty() ? QStringLiteral("Qt 后端请求失败（错误码 %1）").arg(code) : fallback;
}
