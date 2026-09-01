#include "apiclient.h"

#include <algorithm>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QtMath>

ApiClient::ApiClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // 演示站点围绕北京天安门附近，便于默认坐标 39.90,116.40 能查到 10km 内结果。
    auto add = [&](qint64 id, const QString &name, const QString &addr,
                   double lat, double lng, int price, int total, int idle, double online) {
        StationSummary s;
        s.id = id;
        s.name = name;
        s.address = addr;
        s.latitude = lat;
        s.longitude = lng;
        s.priceCentsPerKwh = price;
        s.status = QStringLiteral("ACTIVE");
        s.totalPiles = total;
        s.availablePiles = idle;
        s.onlineRate = online;
        m_seedStations.push_back(s);
    };
    add(1, QStringLiteral("市民中心充电站"), QStringLiteral("北京市东城区示例路 1 号"),
        39.9050, 116.4000, 125, 20, 8, 90.0);
    add(2, QStringLiteral("中关村充电站"), QStringLiteral("北京市海淀区示例路 8 号"),
        39.9836, 116.3159, 138, 12, 3, 83.3);
    add(3, QStringLiteral("科技园充电站"), QStringLiteral("北京市海淀区示例路 16 号"),
        39.9800, 116.3100, 132, 16, 5, 87.5);
    add(4, QStringLiteral("望京充电站"), QStringLiteral("北京市朝阳区示例路 3 号"),
        39.9960, 116.4700, 129, 10, 2, 80.0);
    add(5, QStringLiteral("上海演示站（应被 10km 滤掉）"), QStringLiteral("上海市浦东新区"),
        31.2304, 121.4737, 150, 8, 8, 100.0);
}

void ApiClient::setBaseUrl(const QString &url)
{
    m_baseUrl = url.trimmed();
    while (m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl.chop(1);
}

void ApiClient::setDemoMode(bool enabled)
{
    m_demoMode = enabled;
}

void ApiClient::setToken(const QString &token)
{
    m_token = token;
}

void ApiClient::clearSession()
{
    m_token.clear();
    m_demoUser = User{};
    m_demoRecharges.clear();
}

QNetworkRequest ApiClient::makeRequest(const QString &path, bool withAuth) const
{
    QNetworkRequest req{QUrl(m_baseUrl + path)};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json; charset=utf-8"));
    req.setTransferTimeout(15000);
    if (withAuth && !m_token.isEmpty()) {
        req.setRawHeader("Authorization",
                         QByteArray("Bearer ") + m_token.toUtf8());
    }
    return req;
}

void ApiClient::login(const QString &phone)
{
    if (m_demoMode) {
        demoLogin(phone);
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("phone"), phone);
    sendJson(QStringLiteral("POST"), QStringLiteral("/user/login"), body, false);
    m_pendingKind = QStringLiteral("login");
}

void ApiClient::fetchProfile()
{
    if (m_demoMode) {
        emit requestStarted();
        QTimer::singleShot(120, this, [this]() {
            emit profileReady(m_demoUser);
            emit requestFinished();
        });
        return;
    }
    m_pendingKind = QStringLiteral("profile");
    get(QStringLiteral("/user/profile"));
}

void ApiClient::updateNickname(const QString &nickname)
{
    if (m_demoMode) {
        m_demoUser.nickname = nickname.trimmed();
        emit requestStarted();
        QTimer::singleShot(120, this, [this]() {
            emit nicknameUpdated(m_demoUser);
            emit requestFinished();
        });
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("nickname"), nickname.trimmed());
    m_pendingKind = QStringLiteral("nickname");
    sendJson(QStringLiteral("PUT"), QStringLiteral("/user/profile"), body, true);
}

void ApiClient::recharge(double amountYuan)
{
    const qint64 cents = yuanToCents(amountYuan);
    if (m_demoMode) {
        emit requestStarted();
        QTimer::singleShot(180, this, [this, cents]() {
            RechargeRecord rec;
            rec.rechargeNo = QStringLiteral("RC") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddhhmmss"));
            rec.amountCents = cents;
            rec.balanceAfterCents = m_demoUser.balanceCents + cents;
            rec.status = QStringLiteral("SUCCESS");
            rec.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            m_demoUser.balanceCents = rec.balanceAfterCents;
            m_demoRecharges.prepend(rec);
            emit rechargeSucceeded(m_demoUser.balanceCents, rec);
            emit requestFinished();
        });
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("amount_cents"), cents);
    // 幂等请求号：同一号重试不会重复加余额
    body.insert(QStringLiteral("client_request_id"),
                QStringLiteral("qt-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_pendingKind = QStringLiteral("recharge");
    sendJson(QStringLiteral("POST"), QStringLiteral("/user/recharge"), body, true);
}

void ApiClient::fetchRechargeRecords()
{
    if (m_demoMode) {
        emit requestStarted();
        QTimer::singleShot(80, this, [this]() {
            emit rechargeRecordsReady(m_demoRecharges);
            emit requestFinished();
        });
        return;
    }
    m_pendingKind = QStringLiteral("rechargeRecords");
    get(QStringLiteral("/user/recharge-records?page=1&page_size=20"));
}

void ApiClient::fetchNearbyStations(double latitude, double longitude, double radiusKm)
{
    if (m_demoMode) {
        demoNearby(latitude, longitude, radiusKm);
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("latitude"), QString::number(latitude, 'f', 7));
    query.addQueryItem(QStringLiteral("longitude"), QString::number(longitude, 'f', 7));
    query.addQueryItem(QStringLiteral("radius_km"), QString::number(radiusKm, 'f', 1));
    m_pendingKind = QStringLiteral("nearby");
    get(QStringLiteral("/stations/nearby?") + query.toString(QUrl::FullyEncoded));
}

void ApiClient::get(const QString &path)
{
    emit requestStarted();
    QNetworkReply *reply = m_nam->get(makeRequest(path, true));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

void ApiClient::sendJson(const QString &method, const QString &path,
                         const QJsonObject &body, bool withAuth)
{
    emit requestStarted();
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = nullptr;
    const QNetworkRequest req = makeRequest(path, withAuth);
    if (method == QLatin1String("POST"))
        reply = m_nam->post(req, payload);
    else if (method == QLatin1String("PUT"))
        reply = m_nam->put(req, payload);
    else
        reply = m_nam->sendCustomRequest(req, method.toLatin1(), payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

void ApiClient::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();
    const Envelope env = parseEnvelope(reply);
    emit requestFinished();

    if (!env.ok()) {
        emit apiFailed(env.code, chineseMessage(env.code, env.message));
        return;
    }

    const QString kind = m_pendingKind;
    m_pendingKind.clear();

    if (kind == QLatin1String("login")) {
        const QJsonObject data = env.data.toObject();
        const User user = parseUser(data.value(QStringLiteral("user")).toObject());
        const QString token = data.value(QStringLiteral("access_token")).toString();
        setToken(token);
        emit loginSucceeded(token, user, data.value(QStringLiteral("is_new_user")).toBool());
        return;
    }
    if (kind == QLatin1String("profile") || kind == QLatin1String("nickname")) {
        const User user = parseUser(env.data.toObject());
        if (kind == QLatin1String("nickname"))
            emit nicknameUpdated(user);
        else
            emit profileReady(user);
        return;
    }
    if (kind == QLatin1String("recharge")) {
        const QJsonObject data = env.data.toObject();
        RechargeRecord rec = parseRecharge(data);
        emit rechargeSucceeded(data.value(QStringLiteral("balance_after_cents")).toVariant().toLongLong(), rec);
        return;
    }
    if (kind == QLatin1String("rechargeRecords")) {
        QVector<RechargeRecord> list;
        QJsonArray items = env.data.toArray();
        if (env.data.isObject())
            items = env.data.toObject().value(QStringLiteral("items")).toArray();
        for (const QJsonValue &v : items)
            list.push_back(parseRecharge(v.toObject()));
        emit rechargeRecordsReady(list);
        return;
    }
    if (kind == QLatin1String("nearby")) {
        QVector<StationSummary> list;
        QJsonArray items = env.data.toArray();
        if (env.data.isObject())
            items = env.data.toObject().value(QStringLiteral("items")).toArray();
        for (const QJsonValue &v : items)
            list.push_back(parseStation(v.toObject()));
        emit nearbyStationsReady(list);
    }
}

ApiClient::Envelope ApiClient::parseEnvelope(QNetworkReply *reply) const
{
    Envelope env;
    env.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && env.httpStatus == 0) {
        env.code = 50001;
        env.message = QStringLiteral("无法连接服务器，请检查地址或改用演示模式");
        return env;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        env.code = 50000;
        env.message = QStringLiteral("服务器返回了无法解析的数据");
        return env;
    }
    const QJsonObject obj = doc.object();
    env.code = obj.value(QStringLiteral("code")).toInt(-1);
    env.message = obj.value(QStringLiteral("message")).toString();
    env.data = obj.value(QStringLiteral("data"));
    return env;
}

User ApiClient::parseUser(const QJsonObject &obj) const
{
    User u;
    u.id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
    u.phone = obj.value(QStringLiteral("phone")).toString();
    u.nickname = obj.value(QStringLiteral("nickname")).toString();
    u.avatarUrl = obj.value(QStringLiteral("avatar_url")).toString();
    u.balanceCents = obj.value(QStringLiteral("balance_cents")).toVariant().toLongLong();
    u.status = obj.value(QStringLiteral("status")).toString();
    return u;
}

StationSummary ApiClient::parseStation(const QJsonObject &obj) const
{
    StationSummary s;
    s.id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
    s.name = obj.value(QStringLiteral("name")).toString();
    s.address = obj.value(QStringLiteral("address")).toString();
    s.latitude = obj.value(QStringLiteral("latitude")).toDouble();
    s.longitude = obj.value(QStringLiteral("longitude")).toDouble();
    s.priceCentsPerKwh = obj.value(QStringLiteral("price_cents_per_kwh")).toInt();
    s.status = obj.value(QStringLiteral("status")).toString();
    s.totalPiles = obj.value(QStringLiteral("total_piles")).toInt();
    s.availablePiles = obj.value(QStringLiteral("available_piles")).toInt();
    s.onlineRate = obj.value(QStringLiteral("online_rate")).toDouble();
    if (obj.value(QStringLiteral("distance_km")).isNull())
        s.distanceKm = -1;
    else
        s.distanceKm = obj.value(QStringLiteral("distance_km")).toDouble();
    return s;
}

RechargeRecord ApiClient::parseRecharge(const QJsonObject &obj) const
{
    RechargeRecord r;
    r.rechargeNo = obj.value(QStringLiteral("recharge_no")).toString();
    r.amountCents = obj.value(QStringLiteral("amount_cents")).toVariant().toLongLong();
    r.balanceAfterCents = obj.value(QStringLiteral("balance_after_cents")).toVariant().toLongLong();
    r.status = obj.value(QStringLiteral("status")).toString();
    r.createdAt = obj.value(QStringLiteral("created_at")).toString();
    return r;
}

QString ApiClient::chineseMessage(int code, const QString &fallback) const
{
    switch (code) {
    case 10001: return QStringLiteral("参数错误，请检查手机号或输入内容");
    case 20001: return QStringLiteral("登录已过期，请重新登录");
    case 20003: return QStringLiteral("该账号已被冻结，无法登录");
    case 20004: return QStringLiteral("账号或密码错误");
    case 50001: return fallback.isEmpty()
                   ? QStringLiteral("服务暂不可用")
                   : fallback;
    default:
        if (!fallback.isEmpty() && fallback != QLatin1String("success"))
            return fallback;
        return QStringLiteral("请求失败（错误码 %1）").arg(code);
    }
}

void ApiClient::demoLogin(const QString &phone)
{
    emit requestStarted();
    QTimer::singleShot(200, this, [this, phone]() {
        // 说明书：已存在则登录，不存在则自动注册，默认昵称「用户+后四位」
        if (phone == QLatin1String("13800000000")) {
            emit apiFailed(20003, chineseMessage(20003, QString()));
            emit requestFinished();
            return;
        }
        m_demoUser.id = 1;
        m_demoUser.phone = phone;
        m_demoUser.nickname = QStringLiteral("用户") + phone.right(4);
        m_demoUser.avatarUrl.clear();
        m_demoUser.balanceCents = 0;
        m_demoUser.status = QStringLiteral("NORMAL");
        m_token = QStringLiteral("demo-token-") + phone;
        emit requestFinished();
        emit loginSucceeded(m_token, m_demoUser, true);
    });
}

void ApiClient::demoNearby(double latitude, double longitude, double radiusKm)
{
    emit requestStarted();
    QTimer::singleShot(180, this, [this, latitude, longitude, radiusKm]() {
        QVector<StationSummary> result;
        for (StationSummary s : m_seedStations) {
            s.distanceKm = haversineKm(latitude, longitude, s.latitude, s.longitude);
            if (s.distanceKm <= radiusKm)
                result.push_back(s);
        }
        std::sort(result.begin(), result.end(), [](const StationSummary &a, const StationSummary &b) {
            return a.distanceKm < b.distanceKm;
        });
        emit nearbyStationsReady(result);
        emit requestFinished();
    });
}

double ApiClient::haversineKm(double lat1, double lon1, double lat2, double lon2) const
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
