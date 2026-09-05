#include "apiclient.h"

#include <algorithm>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QVariant>
#include <QtMath>

namespace {
constexpr int kReservationTimeoutSeconds = 900;

QDateTime parseIsoUtc(const QString &iso)
{
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid())
        dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    return dt.isValid() ? dt.toUTC() : dt;
}
}

ApiClient::ApiClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // 演示站点围绕北京天安门附近，便于默认坐标 39.90,116.40 能查到 10km 内结果。
    auto add = [&](qint64 id, const QString &name, const QString &addr,
                   double lat, double lng, int price, int total, int idle, double online,
                   const QString &op, const QString &district, const QString &region = QString(),
                   const QString &locationType = QString()) {
        StationSummary s;
        s.id = id;
        s.name = name;
        s.address = addr;
        s.latitude = lat;
        s.longitude = lng;
        s.priceCentsPerKwh = price;
        s.status = QStringLiteral("ACTIVE");
        s.operatorName = op;
        s.district = district;
        s.regionScope = region;
        s.locationType = locationType;
        s.hasCoordinates = true;
        s.isBookable = (price > 0 && idle > 0);
        s.totalPiles = total;
        s.availablePiles = idle;
        s.onlineRate = online;
        m_seedStations.push_back(s);
    };
    add(1, QStringLiteral("市民中心充电站"), QStringLiteral("北京市东城区示例路 1 号"),
        39.9050, 116.4000, 125, 20, 8, 90.0,
        QStringLiteral("示例运营商"), QStringLiteral("东城区"),
        QStringLiteral("二环至三环"), QStringLiteral("公共停车场"));
    add(2, QStringLiteral("中关村充电站"), QStringLiteral("北京市海淀区示例路 8 号"),
        39.9836, 116.3159, 138, 12, 3, 83.3,
        QStringLiteral("国家电网"), QStringLiteral("海淀区"),
        QStringLiteral("三环至四环"), QStringLiteral("公共停车场"));
    add(3, QStringLiteral("科技园充电站"), QStringLiteral("北京市海淀区示例路 16 号"),
        39.9800, 116.3100, 132, 16, 5, 87.5,
        QStringLiteral("特来电"), QStringLiteral("海淀区"),
        QStringLiteral("三环至四环"), QStringLiteral("写字楼"));
    add(4, QStringLiteral("望京充电站"), QStringLiteral("北京市朝阳区示例路 3 号"),
        39.9960, 116.4700, 129, 10, 2, 80.0,
        QStringLiteral("中石化"), QStringLiteral("朝阳区"),
        QStringLiteral("四环至五环"), QStringLiteral("公共停车场"));
    add(5, QStringLiteral("上海演示站（应被 10km 滤掉）"), QStringLiteral("上海市浦东新区"),
        31.2304, 121.4737, 150, 8, 8, 100.0,
        QStringLiteral("国家电网"), QStringLiteral("浦东新区"),
        QStringLiteral("外环"), QStringLiteral("公共停车场"));

    StationSummary catalog;
    catalog.id = 101;
    catalog.name = QStringLiteral("广泽桥加油充电站");
    catalog.address = QStringLiteral("朝阳区望京地区屏翠东路，北五环内环望京出口（地上）");
    catalog.operatorName = QStringLiteral("中石化");
    catalog.district = QStringLiteral("朝阳区");
    catalog.regionScope = QStringLiteral("四环至五环");
    catalog.locationType = QStringLiteral("加油站");
    catalog.status = QStringLiteral("ACTIVE");
    catalog.hasCoordinates = false;
    catalog.priceCentsPerKwh = -1;
    m_seedStations.push_back(catalog);
    m_seedStationsInitial = m_seedStations;
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
    m_demoOrders.clear();
    m_demoPilesByStation.clear();
    m_demoPilesReady.clear();
    m_seedStations = m_seedStationsInitial;
    m_nextDemoOrderId = 200;
}

QNetworkRequest ApiClient::makeRequest(const QString &path, bool withAuth, bool jsonContent) const
{
    QNetworkRequest req{QUrl(m_baseUrl + path)};
    if (jsonContent) {
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json; charset=utf-8"));
    }
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
    m_pendingKind = QStringLiteral("login");
    sendJson(QStringLiteral("POST"), QStringLiteral("/user/login"), body, false);
}

void ApiClient::fetchProfile()
{
    if (m_demoMode) {
        succeedLater([this]() { emit profileReady(m_demoUser); }, 120);
        return;
    }
    m_pendingKind = QStringLiteral("profile");
    get(QStringLiteral("/user/profile"));
}

void ApiClient::updateNickname(const QString &nickname)
{
    if (m_demoMode) {
        m_demoUser.nickname = nickname.trimmed();
        succeedLater([this]() { emit nicknameUpdated(m_demoUser); }, 120);
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("nickname"), nickname.trimmed());
    m_pendingKind = QStringLiteral("nickname");
    sendJson(QStringLiteral("PUT"), QStringLiteral("/user/profile"), body, true);
}

void ApiClient::uploadAvatar(const QString &filePath)
{
    if (m_demoMode) {
        demoUploadAvatar(filePath);
        return;
    }

    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile() || info.size() <= 0) {
        emit apiFailed(10002, chineseMessage(10002, QStringLiteral("请选择有效的头像文件")));
        return;
    }
    if (info.size() > 2 * 1024 * 1024) {
        emit apiFailed(10002, chineseMessage(10002, QStringLiteral("头像不能超过 2MB")));
        return;
    }

    auto *file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        emit apiFailed(10002, chineseMessage(10002, QStringLiteral("无法读取头像文件")));
        return;
    }

    const QString mime = QMimeDatabase().mimeTypeForFile(info).name();
    const QString suffix = info.suffix().toLower();
    const bool okMime = mime == QLatin1String("image/jpeg")
                        || mime == QLatin1String("image/png")
                        || mime == QLatin1String("image/webp");
    const bool okExt = suffix == QLatin1String("jpg")
                       || suffix == QLatin1String("jpeg")
                       || suffix == QLatin1String("png")
                       || suffix == QLatin1String("webp");
    if (!okMime && !okExt) {
        delete file;
        emit apiFailed(10002, chineseMessage(10002, QStringLiteral("仅支持 JPEG / PNG / WEBP")));
        return;
    }

    QString contentType = mime;
    if (!okMime) {
        if (suffix == QLatin1String("png"))
            contentType = QStringLiteral("image/png");
        else if (suffix == QLatin1String("webp"))
            contentType = QStringLiteral("image/webp");
        else
            contentType = QStringLiteral("image/jpeg");
    }

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"avatar\"; filename=\"%1\"")
                       .arg(info.fileName()));
    part.setBodyDevice(file);
    file->setParent(multi);
    multi->append(part);

    m_pendingKind = QStringLiteral("avatar");
    emit requestStarted();
    QNetworkReply *reply = m_nam->post(makeRequest(QStringLiteral("/user/avatar"), true, false), multi);
    multi->setParent(reply);
    reply->setProperty("pendingKind", m_pendingKind);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

void ApiClient::recharge(double amountYuan)
{
    const qint64 cents = yuanToCents(amountYuan);
    if (m_demoMode) {
        int code = 0;
        if (!demoUserWritable(&code)) {
            failDemo(code);
            return;
        }
        succeedLater([this, cents]() {
            RechargeRecord rec;
            rec.rechargeNo = QStringLiteral("RC") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddhhmmss"));
            rec.amountCents = cents;
            rec.balanceAfterCents = m_demoUser.balanceCents + cents;
            rec.status = QStringLiteral("SUCCESS");
            rec.createdAt = nowUtcIso();
            m_demoUser.balanceCents = rec.balanceAfterCents;
            m_demoRecharges.prepend(rec);
            emit rechargeSucceeded(m_demoUser.balanceCents, rec);
        }, 180);
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("amount_cents"), cents);
    body.insert(QStringLiteral("client_request_id"),
                QStringLiteral("qt-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_pendingKind = QStringLiteral("recharge");
    sendJson(QStringLiteral("POST"), QStringLiteral("/user/recharge"), body, true);
}

void ApiClient::fetchRechargeRecords()
{
    if (m_demoMode) {
        succeedLater([this]() { emit rechargeRecordsReady(m_demoRecharges); }, 80);
        return;
    }
    m_pendingKind = QStringLiteral("rechargeRecords");
    get(QStringLiteral("/user/recharge-records?page=1&page_size=20"));
}

void ApiClient::fetchNearbyStations(double latitude, double longitude, double radiusKm, bool availableOnly)
{
    if (m_demoMode) {
        demoNearby(latitude, longitude, radiusKm, availableOnly);
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("latitude"), QString::number(latitude, 'f', 7));
    query.addQueryItem(QStringLiteral("longitude"), QString::number(longitude, 'f', 7));
    query.addQueryItem(QStringLiteral("radius_km"), QString::number(radiusKm, 'f', 1));
    if (availableOnly)
        query.addQueryItem(QStringLiteral("available_only"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
    m_pendingKind = QStringLiteral("nearby");
    get(QStringLiteral("/stations/nearby?") + query.toString(QUrl::FullyEncoded));
}

void ApiClient::fetchStationFilterOptions()
{
    if (m_demoMode) {
        demoStationFilterOptions();
        return;
    }
    m_pendingKind = QStringLiteral("filterOptions");
    get(QStringLiteral("/stations/filter-options"));
}

void ApiClient::fetchStations(const QString &keyword, const QString &district, const QString &operatorName,
                             const QString &regionScope, const QString &locationType, bool bookableOnly)
{
    if (m_demoMode) {
        demoSearchStations(keyword, district, operatorName, regionScope, locationType, bookableOnly);
        return;
    }
    QUrlQuery query;
    if (!keyword.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("keyword"), keyword.trimmed());
    if (!district.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("district"), district.trimmed());
    if (!operatorName.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("operator_name"), operatorName.trimmed());
    if (!regionScope.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("region_scope"), regionScope.trimmed());
    if (!locationType.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("location_type"), locationType.trimmed());
    if (bookableOnly)
        query.addQueryItem(QStringLiteral("bookable_only"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("page_size"), QStringLiteral("50"));
    m_pendingKind = QStringLiteral("stationSearch");
    get(QStringLiteral("/stations?") + query.toString(QUrl::FullyEncoded));
}

void ApiClient::fetchStationDetail(qint64 stationId)
{
    if (m_demoMode) {
        demoStationDetail(stationId);
        return;
    }
    m_pendingKind = QStringLiteral("stationDetail");
    get(QStringLiteral("/stations/%1").arg(stationId));
}

void ApiClient::fetchStationPiles(qint64 stationId, const QString &status, const QString &pileType)
{
    if (m_demoMode) {
        demoStationPiles(stationId, status, pileType);
        return;
    }
    m_pendingStationId = stationId;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("page_size"), QStringLiteral("100"));
    if (!status.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("status"), status.trimmed());
    if (!pileType.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("pile_type"), pileType.trimmed());
    m_pendingKind = QStringLiteral("stationPiles");
    get(QStringLiteral("/stations/%1/piles?").arg(stationId) + query.toString(QUrl::FullyEncoded));
}

void ApiClient::fetchPileDetail(qint64 pileId)
{
    if (m_demoMode) {
        demoPileDetail(pileId);
        return;
    }
    m_pendingKind = QStringLiteral("pileDetail");
    get(QStringLiteral("/piles/%1").arg(pileId));
}

void ApiClient::fetchOrders(const QString &status)
{
    if (m_demoMode) {
        demoOrders(status);
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("page_size"), QStringLiteral("50"));
    if (!status.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("status"), status.trimmed());
    m_pendingKind = QStringLiteral("orders");
    get(QStringLiteral("/orders?") + query.toString(QUrl::FullyEncoded));
}

void ApiClient::fetchActiveOrder()
{
    if (m_demoMode) {
        demoActiveOrder();
        return;
    }
    m_pendingKind = QStringLiteral("activeOrder");
    get(QStringLiteral("/orders/active"));
}

void ApiClient::fetchOrderDetail(qint64 orderId)
{
    if (m_demoMode) {
        demoOrderDetail(orderId);
        return;
    }
    m_pendingKind = QStringLiteral("orderDetail");
    get(QStringLiteral("/orders/%1").arg(orderId));
}

void ApiClient::createOrder(qint64 pileId)
{
    if (m_demoMode) {
        demoCreateOrder(pileId);
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("pile_id"), pileId);
    m_pendingKind = QStringLiteral("createOrder");
    sendJson(QStringLiteral("POST"), QStringLiteral("/orders"), body, true);
}

void ApiClient::reserveOrder(qint64 orderId)
{
    if (m_demoMode) {
        demoReserveOrder(orderId);
        return;
    }
    m_pendingKind = QStringLiteral("orderAction");
    sendJson(QStringLiteral("POST"), QStringLiteral("/orders/%1/reserve").arg(orderId), {}, true);
}

void ApiClient::startOrder(qint64 orderId)
{
    if (m_demoMode) {
        demoStartOrder(orderId);
        return;
    }
    m_pendingKind = QStringLiteral("orderAction");
    sendJson(QStringLiteral("POST"), QStringLiteral("/orders/%1/start").arg(orderId), {}, true);
}

void ApiClient::stopOrder(qint64 orderId)
{
    if (m_demoMode) {
        demoStopOrder(orderId);
        return;
    }
    m_pendingKind = QStringLiteral("orderAction");
    sendJson(QStringLiteral("POST"), QStringLiteral("/orders/%1/stop").arg(orderId), {}, true);
}

void ApiClient::settleOrder(qint64 orderId)
{
    if (m_demoMode) {
        demoSettleOrder(orderId);
        return;
    }
    m_pendingKind = QStringLiteral("settle");
    sendJson(QStringLiteral("POST"), QStringLiteral("/orders/%1/settle").arg(orderId), {}, true);
}

void ApiClient::cancelOrder(qint64 orderId, const QString &reason)
{
    if (m_demoMode) {
        demoCancelOrder(orderId, reason);
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("reason"), reason.trimmed().isEmpty()
                                              ? QStringLiteral("用户取消预约")
                                              : reason.trimmed());
    m_pendingKind = QStringLiteral("orderAction");
    sendJson(QStringLiteral("POST"), QStringLiteral("/orders/%1/cancel").arg(orderId), body, true);
}

void ApiClient::expireReservationForDemo()
{
    if (!m_demoMode) {
        emit apiFailed(10002, QStringLiteral("仅演示模式可模拟预约超时"));
        return;
    }
    lazyExpireDemoReservations();
    ChargingOrder *order = nullptr;
    for (ChargingOrder &o : m_demoOrders) {
        if (o.status == QLatin1String("RESERVED")) {
            order = &o;
            break;
        }
    }
    if (!order) {
        failDemo(40003);
        return;
    }
    succeedLater([this, id = order->id]() {
        ChargingOrder *current = demoOrderById(id);
        if (!current) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        current->reservationExpiresAt = QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODate);
        lazyExpireDemoReservations();
        if (ChargingOrder *expired = demoOrderById(id))
            emit orderUpdated(*expired);
    }, 120);
}

void ApiClient::get(const QString &path)
{
    emit requestStarted();
    QNetworkReply *reply = m_nam->get(makeRequest(path, true));
    reply->setProperty("pendingKind", m_pendingKind);
    reply->setProperty("pendingStationId", QVariant::fromValue(m_pendingStationId));
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

    reply->setProperty("pendingKind", m_pendingKind);
    reply->setProperty("pendingStationId", QVariant::fromValue(m_pendingStationId));
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

    const QString kind = reply->property("pendingKind").toString();
    const qint64 stationId = reply->property("pendingStationId").toLongLong();
    m_pendingKind.clear();

    if (kind == QLatin1String("login")) {
        const QJsonObject data = env.data.toObject();
        const User user = parseUser(data.value(QStringLiteral("user")).toObject());
        const QString token = data.value(QStringLiteral("access_token")).toString();
        setToken(token);
        emit loginSucceeded(token, user, data.value(QStringLiteral("is_new_user")).toBool());
        return;
    }
    if (kind == QLatin1String("profile") || kind == QLatin1String("nickname") || kind == QLatin1String("avatar")) {
        const User user = parseUser(env.data.toObject());
        if (kind == QLatin1String("nickname"))
            emit nicknameUpdated(user);
        else if (kind == QLatin1String("avatar"))
            emit avatarUploaded(user);
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
        int ignored = 0;
        emit nearbyStationsReady(parseStationList(env.data, &ignored));
        return;
    }
    if (kind == QLatin1String("filterOptions")) {
        emit stationFilterOptionsReady(parseFilterOptions(env.data.toObject()));
        return;
    }
    if (kind == QLatin1String("stationSearch")) {
        int total = 0;
        const QVector<StationSummary> list = parseStationList(env.data, &total);
        emit stationsSearchReady(list, total);
        return;
    }
    if (kind == QLatin1String("stationDetail")) {
        emit stationDetailReady(parseStation(env.data.toObject()));
        return;
    }
    if (kind == QLatin1String("stationPiles")) {
        QVector<ChargingPile> list;
        QJsonArray items = env.data.toArray();
        if (env.data.isObject())
            items = env.data.toObject().value(QStringLiteral("items")).toArray();
        for (const QJsonValue &v : items)
            list.push_back(parsePile(v.toObject()));
        emit stationPilesReady(stationId, list);
        return;
    }
    if (kind == QLatin1String("pileDetail")) {
        emit pileDetailReady(parsePile(env.data.toObject()));
        return;
    }
    if (kind == QLatin1String("orders")) {
        emit ordersReady(parseOrderList(env.data));
        return;
    }
    if (kind == QLatin1String("activeOrder")) {
        if (env.data.isNull() || env.data.isUndefined() || (env.data.isObject() && env.data.toObject().isEmpty())) {
            emit activeOrderReady(false, ChargingOrder{});
            return;
        }
        emit activeOrderReady(true, parseOrder(env.data.toObject()));
        return;
    }
    if (kind == QLatin1String("createOrder")) {
        emit orderCreated(parseOrder(env.data.toObject()));
        return;
    }
    if (kind == QLatin1String("orderAction") || kind == QLatin1String("orderDetail")) {
        emit orderUpdated(parseOrder(env.data.toObject()));
        return;
    }
    if (kind == QLatin1String("settle")) {
        const QJsonObject data = env.data.toObject();
        ChargingOrder order = parseOrder(data.value(QStringLiteral("order")).toObject());
        const qint64 balance = data.value(QStringLiteral("balance_cents")).toVariant().toLongLong();
        emit orderSettled(order, balance);
        return;
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
    s.priceCentsPerKwh = obj.value(QStringLiteral("price_cents_per_kwh")).isNull()
                             ? -1
                             : obj.value(QStringLiteral("price_cents_per_kwh")).toInt();
    s.status = obj.value(QStringLiteral("status")).toString();
    s.operatorName = obj.value(QStringLiteral("operator_name")).toString();
    s.district = obj.value(QStringLiteral("district")).toString();
    s.regionScope = obj.value(QStringLiteral("region_scope")).toString();
    s.locationType = obj.value(QStringLiteral("location_type")).toString();
    const bool latOk = !obj.value(QStringLiteral("latitude")).isNull()
                       && obj.contains(QStringLiteral("latitude"));
    const bool lngOk = !obj.value(QStringLiteral("longitude")).isNull()
                       && obj.contains(QStringLiteral("longitude"));
    s.hasCoordinates = obj.contains(QStringLiteral("has_coordinates"))
                           ? obj.value(QStringLiteral("has_coordinates")).toBool()
                           : (latOk && lngOk);
    s.isBookable = obj.value(QStringLiteral("is_bookable")).toBool();
    s.totalPiles = obj.value(QStringLiteral("total_piles")).toInt();
    s.availablePiles = obj.value(QStringLiteral("available_piles")).toInt();
    s.onlineRate = obj.value(QStringLiteral("online_rate")).toDouble();
    if (obj.value(QStringLiteral("distance_km")).isNull())
        s.distanceKm = -1;
    else
        s.distanceKm = obj.value(QStringLiteral("distance_km")).toDouble();
    return s;
}

StationFilterOptions ApiClient::parseFilterOptions(const QJsonObject &obj) const
{
    StationFilterOptions o;
    auto toList = [](const QJsonValue &value) {
        QStringList list;
        for (const QJsonValue &item : value.toArray()) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty())
                list.push_back(text);
        }
        return list;
    };
    o.operatorNames = toList(obj.value(QStringLiteral("operator_names")));
    o.districts = toList(obj.value(QStringLiteral("districts")));
    o.regionScopes = toList(obj.value(QStringLiteral("region_scopes")));
    o.locationTypes = toList(obj.value(QStringLiteral("location_types")));
    o.stationCount = obj.value(QStringLiteral("station_count")).toInt();
    o.withCoordinatesCount = obj.value(QStringLiteral("with_coordinates_count")).toInt();
    o.withoutCoordinatesCount = obj.value(QStringLiteral("without_coordinates_count")).toInt();
    return o;
}

QVector<StationSummary> ApiClient::parseStationList(const QJsonValue &data, int *total) const
{
    QVector<StationSummary> list;
    QJsonArray items = data.toArray();
    int counted = 0;
    if (data.isObject()) {
        const QJsonObject obj = data.toObject();
        items = obj.value(QStringLiteral("items")).toArray();
        counted = obj.value(QStringLiteral("pagination")).toObject()
                      .value(QStringLiteral("total")).toInt(items.size());
    } else {
        counted = items.size();
    }
    for (const QJsonValue &v : items)
        list.push_back(parseStation(v.toObject()));
    if (counted < list.size())
        counted = list.size();
    if (total)
        *total = counted;
    return list;
}

QVector<ChargingOrder> ApiClient::parseOrderList(const QJsonValue &data) const
{
    QVector<ChargingOrder> list;
    QJsonArray items = data.toArray();
    if (data.isObject())
        items = data.toObject().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &v : items)
        list.push_back(parseOrder(v.toObject()));
    return list;
}

ChargingPile ApiClient::parsePile(const QJsonObject &obj) const
{
    ChargingPile p;
    p.id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
    p.pileNo = obj.value(QStringLiteral("pile_no")).toString();
    p.stationId = obj.value(QStringLiteral("station_id")).toVariant().toLongLong();
    p.stationName = obj.value(QStringLiteral("station_name")).toString();
    p.pileType = obj.value(QStringLiteral("pile_type")).toString();
    p.ratedPowerW = obj.value(QStringLiteral("rated_power_w")).toInt();
    p.status = obj.value(QStringLiteral("status")).toString();
    p.lastHeartbeatAt = obj.value(QStringLiteral("last_heartbeat_at")).toString();
    p.totalChargeCount = obj.value(QStringLiteral("total_charge_count")).toInt();
    p.totalChargeDurationSeconds = obj.value(QStringLiteral("total_charge_duration_seconds")).toInt();
    return p;
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

ChargingOrder ApiClient::parseOrder(const QJsonObject &obj) const
{
    ChargingOrder o;
    o.id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
    o.orderNo = obj.value(QStringLiteral("order_no")).toString();
    o.userId = obj.value(QStringLiteral("user_id")).toVariant().toLongLong();
    o.stationId = obj.value(QStringLiteral("station_id")).toVariant().toLongLong();
    o.stationName = obj.value(QStringLiteral("station_name")).toString();
    o.pileId = obj.value(QStringLiteral("pile_id")).toVariant().toLongLong();
    o.pileNo = obj.value(QStringLiteral("pile_no")).toString();
    o.status = obj.value(QStringLiteral("status")).toString();
    o.unitPriceCentsPerKwh = obj.value(QStringLiteral("unit_price_cents_per_kwh")).toInt();
    o.energyWh = obj.value(QStringLiteral("energy_wh")).toVariant().toLongLong();
    o.durationSeconds = obj.value(QStringLiteral("duration_seconds")).toInt();
    o.amountCents = obj.value(QStringLiteral("amount_cents")).toVariant().toLongLong();
    o.reservedAt = obj.value(QStringLiteral("reserved_at")).toString();
    o.reservationExpiresAt = obj.value(QStringLiteral("reservation_expires_at")).toString();
    o.startedAt = obj.value(QStringLiteral("started_at")).toString();
    o.stoppedAt = obj.value(QStringLiteral("stopped_at")).toString();
    o.settledAt = obj.value(QStringLiteral("settled_at")).toString();
    o.cancelledAt = obj.value(QStringLiteral("cancelled_at")).toString();
    o.cancelReason = obj.value(QStringLiteral("cancel_reason")).toString();
    o.createdAt = obj.value(QStringLiteral("created_at")).toString();
    o.updatedAt = obj.value(QStringLiteral("updated_at")).toString();
    o.ratedPowerW = obj.value(QStringLiteral("rated_power_w")).toInt();
    return o;
}

QString ApiClient::chineseMessage(int code, const QString &fallback) const
{
    switch (code) {
    case 10001: return QStringLiteral("参数错误，请检查手机号或输入内容");
    case 10002: return fallback.isEmpty() ? QStringLiteral("请求无效") : fallback;
    case 20001: return QStringLiteral("登录已过期，请重新登录");
    case 20003: return QStringLiteral("该账号已被冻结，无法进行此操作");
    case 20004: return QStringLiteral("账号或密码错误");
    case 30001: return QStringLiteral("站点、电桩或订单不存在");
    case 40001: return QStringLiteral("您已有未完成订单，请先完成或取消后再预约");
    case 40002: return QStringLiteral("该电桩当前不可预约");
    case 40003: return QStringLiteral("当前订单状态不允许此操作");
    case 40004: return QStringLiteral("订单不属于当前用户");
    case 40005: return QStringLiteral("余额不足，请先充值后再结算");
    case 40007: return QStringLiteral("重复请求，请稍后再试");
    case 40009: return QStringLiteral("预约已超时，电桩已释放，请重新预约");
    case 50001: return fallback.isEmpty()
                   ? QStringLiteral("服务暂不可用")
                   : fallback;
    default:
        if (!fallback.isEmpty() && fallback != QLatin1String("success"))
            return fallback;
        return QStringLiteral("请求失败（错误码 %1）").arg(code);
    }
}

QString ApiClient::nowUtcIso() const
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

void ApiClient::succeedLater(const std::function<void()> &fn, int delayMs)
{
    emit requestStarted();
    QTimer::singleShot(delayMs, this, [this, fn]() {
        fn();
        emit requestFinished();
    });
}

void ApiClient::failDemo(int code)
{
    succeedLater([this, code]() {
        emit apiFailed(code, chineseMessage(code, QString()));
    }, 120);
}

bool ApiClient::demoUserWritable(int *code) const
{
    if (m_demoUser.status == QLatin1String("FROZEN")) {
        if (code)
            *code = 20003;
        return false;
    }
    return true;
}

void ApiClient::demoLogin(const QString &phone)
{
    emit requestStarted();
    QTimer::singleShot(200, this, [this, phone]() {
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
        seedDemoHistoryIfNeeded();
        emit requestFinished();
        emit loginSucceeded(m_token, m_demoUser, true);
    });
}

void ApiClient::demoUploadAvatar(const QString &filePath)
{
    QFileInfo info(filePath);
    if (!info.exists() || info.size() <= 0) {
        failDemo(10002);
        return;
    }
    if (info.size() > 2 * 1024 * 1024) {
        emit requestStarted();
        QTimer::singleShot(80, this, [this]() {
            emit apiFailed(10002, chineseMessage(10002, QStringLiteral("头像不能超过 2MB")));
            emit requestFinished();
        });
        return;
    }
    succeedLater([this, filePath]() {
        m_demoUser.avatarUrl = filePath;
        emit avatarUploaded(m_demoUser);
    }, 160);
}

void ApiClient::demoNearby(double latitude, double longitude, double radiusKm, bool availableOnly)
{
    succeedLater([this, latitude, longitude, radiusKm, availableOnly]() {
        lazyExpireDemoReservations();
        QVector<StationSummary> result;
        for (StationSummary s : m_seedStations) {
            if (!s.hasCoordinates)
                continue;
            s.distanceKm = haversineKm(latitude, longitude, s.latitude, s.longitude);
            if (s.distanceKm > radiusKm)
                continue;
            if (availableOnly && s.availablePiles <= 0)
                continue;
            result.push_back(s);
        }
        std::sort(result.begin(), result.end(), [](const StationSummary &a, const StationSummary &b) {
            return a.distanceKm < b.distanceKm;
        });
        emit nearbyStationsReady(result);
    }, 180);
}

void ApiClient::demoStationFilterOptions()
{
    succeedLater([this]() {
        StationFilterOptions options;
        QStringList operators;
        QStringList districts;
        QStringList regions;
        QStringList locations;
        int withCoords = 0;
        for (const StationSummary &s : m_seedStations) {
            if (!s.operatorName.isEmpty() && !operators.contains(s.operatorName))
                operators.push_back(s.operatorName);
            if (!s.district.isEmpty() && !districts.contains(s.district))
                districts.push_back(s.district);
            if (!s.regionScope.isEmpty() && !regions.contains(s.regionScope))
                regions.push_back(s.regionScope);
            if (!s.locationType.isEmpty() && !locations.contains(s.locationType))
                locations.push_back(s.locationType);
            if (s.hasCoordinates)
                ++withCoords;
        }
        operators.sort();
        districts.sort();
        regions.sort();
        locations.sort();
        options.operatorNames = operators;
        options.districts = districts;
        options.regionScopes = regions;
        options.locationTypes = locations;
        options.stationCount = m_seedStations.size();
        options.withCoordinatesCount = withCoords;
        options.withoutCoordinatesCount = m_seedStations.size() - withCoords;
        emit stationFilterOptionsReady(options);
    }, 80);
}

void ApiClient::demoSearchStations(const QString &keyword, const QString &district, const QString &operatorName,
                                  const QString &regionScope, const QString &locationType, bool bookableOnly)
{
    succeedLater([this, keyword, district, operatorName, regionScope, locationType, bookableOnly]() {
        lazyExpireDemoReservations();
        QVector<StationSummary> result;
        const QString kw = keyword.trimmed();
        const QString dist = district.trimmed();
        const QString op = operatorName.trimmed();
        const QString region = regionScope.trimmed();
        const QString loc = locationType.trimmed();
        for (StationSummary s : m_seedStations) {
            if (!dist.isEmpty() && s.district != dist)
                continue;
            if (!op.isEmpty() && s.operatorName != op)
                continue;
            if (!region.isEmpty() && s.regionScope != region)
                continue;
            if (!loc.isEmpty() && s.locationType != loc)
                continue;
            if (bookableOnly && !s.isBookable)
                continue;
            if (!kw.isEmpty()) {
                const bool hit = s.name.contains(kw, Qt::CaseInsensitive)
                                 || s.address.contains(kw, Qt::CaseInsensitive)
                                 || s.operatorName.contains(kw, Qt::CaseInsensitive);
                if (!hit)
                    continue;
            }
            s.distanceKm = -1;
            result.push_back(s);
        }
        emit stationsSearchReady(result, result.size());
    }, 140);
}

void ApiClient::demoStationDetail(qint64 stationId)
{
    succeedLater([this, stationId]() {
        lazyExpireDemoReservations();
        const StationSummary *station = demoStationById(stationId);
        if (!station) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        emit stationDetailReady(*station);
    }, 100);
}

void ApiClient::demoStationPiles(qint64 stationId, const QString &status, const QString &pileType)
{
    succeedLater([this, stationId, status, pileType]() {
        lazyExpireDemoReservations();
        const StationSummary *station = demoStationById(stationId);
        if (!station) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        ensureDemoPiles(stationId);
        QVector<ChargingPile> piles = m_demoPilesByStation.value(stationId);
        const QString st = status.trimmed();
        const QString type = pileType.trimmed();
        piles.erase(std::remove_if(piles.begin(), piles.end(), [&](const ChargingPile &p) {
            if (!st.isEmpty() && p.status != st)
                return true;
            if (!type.isEmpty() && p.pileType != type)
                return true;
            return false;
        }), piles.end());
        emit stationPilesReady(stationId, piles);
    }, 160);
}

void ApiClient::demoPileDetail(qint64 pileId)
{
    succeedLater([this, pileId]() {
        lazyExpireDemoReservations();
        ChargingPile *pile = demoPileById(pileId);
        if (!pile) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        emit pileDetailReady(*pile);
    }, 100);
}

void ApiClient::demoOrders(const QString &status)
{
    succeedLater([this, status]() {
        lazyExpireDemoReservations();
        seedDemoHistoryIfNeeded();
        QVector<ChargingOrder> list = m_demoOrders;
        const QString st = status.trimmed();
        if (!st.isEmpty()) {
            list.erase(std::remove_if(list.begin(), list.end(), [&](const ChargingOrder &o) {
                return o.status != st;
            }), list.end());
        }
        emit ordersReady(list);
    }, 140);
}

void ApiClient::demoActiveOrder()
{
    succeedLater([this]() {
        lazyExpireDemoReservations();
        for (const ChargingOrder &o : m_demoOrders) {
            if (isActiveOrderStatus(o.status)) {
                emit activeOrderReady(true, o);
                return;
            }
        }
        emit activeOrderReady(false, ChargingOrder{});
    }, 100);
}

void ApiClient::demoOrderDetail(qint64 orderId)
{
    succeedLater([this, orderId]() {
        lazyExpireDemoReservations();
        ChargingOrder *order = demoOrderById(orderId);
        if (!order) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        emit orderUpdated(*order);
    }, 100);
}

void ApiClient::demoCreateOrder(qint64 pileId)
{
    int frozen = 0;
    if (!demoUserWritable(&frozen)) {
        failDemo(frozen);
        return;
    }
    succeedLater([this, pileId]() {
        lazyExpireDemoReservations();
        if (demoHasActiveOrder()) {
            emit apiFailed(40001, chineseMessage(40001, QString()));
            return;
        }
        ChargingPile *pile = demoPileById(pileId);
        if (!pile) {
            for (const StationSummary &s : m_seedStations)
                ensureDemoPiles(s.id);
            pile = demoPileById(pileId);
        }
        if (!pile) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        const StationSummary *station = demoStationById(pile->stationId);
        if (!station || station->status != QLatin1String("ACTIVE") || station->priceCentsPerKwh <= 0) {
            emit apiFailed(40002, chineseMessage(40002, QString()));
            return;
        }
        if (pile->status != QLatin1String("IDLE")) {
            emit apiFailed(40002, chineseMessage(40002, QString()));
            return;
        }
        const ChargingOrder order = makeDemoOrder(*pile, *station);
        m_demoOrders.prepend(order);
        ++m_nextDemoOrderId;
        emit orderCreated(order);
    }, 160);
}

void ApiClient::demoReserveOrder(qint64 orderId)
{
    int frozen = 0;
    if (!demoUserWritable(&frozen)) {
        failDemo(frozen);
        return;
    }
    succeedLater([this, orderId]() {
        lazyExpireDemoReservations();
        ChargingOrder *order = demoOrderById(orderId);
        if (!order) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        if (order->status != QLatin1String("PENDING")) {
            emit apiFailed(40003, chineseMessage(40003, QString()));
            return;
        }
        ChargingPile *pile = demoPileById(order->pileId);
        const StationSummary *station = demoStationById(order->stationId);
        if (!pile || !station || pile->status != QLatin1String("IDLE")
            || station->status != QLatin1String("ACTIVE")) {
            emit apiFailed(40002, chineseMessage(40002, QString()));
            return;
        }
        const QString now = nowUtcIso();
        order->status = QStringLiteral("RESERVED");
        order->reservedAt = now;
        order->reservationExpiresAt = QDateTime::currentDateTimeUtc()
                                          .addSecs(kReservationTimeoutSeconds)
                                          .toString(Qt::ISODate);
        order->updatedAt = now;
        pile->status = QStringLiteral("RESERVED");
        syncDemoStationCounts(pile->stationId);
        emit orderUpdated(*order);
    }, 160);
}

void ApiClient::demoStartOrder(qint64 orderId)
{
    int frozen = 0;
    if (!demoUserWritable(&frozen)) {
        failDemo(frozen);
        return;
    }
    succeedLater([this, orderId]() {
        lazyExpireDemoReservations();
        ChargingOrder *order = demoOrderById(orderId);
        if (!order) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        if (order->status == QLatin1String("CANCELLED")) {
            emit orderUpdated(*order);
            emit apiFailed(40009, chineseMessage(40009, QString()));
            return;
        }
        if (order->status != QLatin1String("RESERVED")) {
            emit apiFailed(40003, chineseMessage(40003, QString()));
            return;
        }
        ChargingPile *pile = demoPileById(order->pileId);
        if (!pile || pile->status != QLatin1String("RESERVED")) {
            emit apiFailed(40003, chineseMessage(40003, QString()));
            return;
        }
        const QString now = nowUtcIso();
        order->status = QStringLiteral("CHARGING");
        order->startedAt = now;
        order->updatedAt = now;
        pile->status = QStringLiteral("CHARGING");
        syncDemoStationCounts(pile->stationId);
        emit orderUpdated(*order);
    }, 160);
}

void ApiClient::demoStopOrder(qint64 orderId)
{
    succeedLater([this, orderId]() {
        ChargingOrder *order = demoOrderById(orderId);
        if (!order) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        if (order->status != QLatin1String("CHARGING")) {
            emit apiFailed(40003, chineseMessage(40003, QString()));
            return;
        }
        ChargingPile *pile = demoPileById(order->pileId);
        QDateTime started = parseIsoUtc(order->startedAt);
        if (!started.isValid())
            started = QDateTime::currentDateTimeUtc().addSecs(-60);
        int seconds = static_cast<int>(started.secsTo(QDateTime::currentDateTimeUtc()));
        if (seconds < 1)
            seconds = 1;
        const int power = order->ratedPowerW > 0 ? order->ratedPowerW
                                                 : (pile ? pile->ratedPowerW : 60000);
        order->durationSeconds = seconds;
        order->energyWh = estimateEnergyWh(power, seconds);
        order->amountCents = estimateAmountCents(order->energyWh, order->unitPriceCentsPerKwh);
        order->status = QStringLiteral("UNPAID");
        order->stoppedAt = nowUtcIso();
        order->updatedAt = order->stoppedAt;
        if (pile) {
            pile->status = QStringLiteral("IDLE");
            pile->totalChargeCount += 1;
            pile->totalChargeDurationSeconds += seconds;
            syncDemoStationCounts(pile->stationId);
        }
        emit orderUpdated(*order);
    }, 180);
}

void ApiClient::demoSettleOrder(qint64 orderId)
{
    int frozen = 0;
    if (!demoUserWritable(&frozen)) {
        failDemo(frozen);
        return;
    }
    succeedLater([this, orderId]() {
        ChargingOrder *order = demoOrderById(orderId);
        if (!order) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        if (order->status == QLatin1String("COMPLETED")) {
            emit orderSettled(*order, m_demoUser.balanceCents);
            return;
        }
        if (order->status != QLatin1String("UNPAID")) {
            emit apiFailed(40003, chineseMessage(40003, QString()));
            return;
        }
        if (m_demoUser.balanceCents < order->amountCents) {
            emit apiFailed(40005, chineseMessage(40005, QString()));
            return;
        }
        m_demoUser.balanceCents -= order->amountCents;
        order->status = QStringLiteral("COMPLETED");
        order->settledAt = nowUtcIso();
        order->updatedAt = order->settledAt;
        emit orderSettled(*order, m_demoUser.balanceCents);
    }, 180);
}

void ApiClient::demoCancelOrder(qint64 orderId, const QString &reason)
{
    succeedLater([this, orderId, reason]() {
        lazyExpireDemoReservations();
        ChargingOrder *order = demoOrderById(orderId);
        if (!order) {
            emit apiFailed(30001, chineseMessage(30001, QString()));
            return;
        }
        if (order->status != QLatin1String("PENDING") && order->status != QLatin1String("RESERVED")) {
            emit apiFailed(40003, chineseMessage(40003, QString()));
            return;
        }
        if (order->status == QLatin1String("RESERVED")) {
            if (ChargingPile *pile = demoPileById(order->pileId)) {
                if (pile->status == QLatin1String("RESERVED"))
                    pile->status = QStringLiteral("IDLE");
                syncDemoStationCounts(pile->stationId);
            }
        }
        order->status = QStringLiteral("CANCELLED");
        order->cancelledAt = nowUtcIso();
        order->cancelReason = reason.trimmed().isEmpty() ? QStringLiteral("用户取消预约") : reason.trimmed();
        order->updatedAt = order->cancelledAt;
        emit orderUpdated(*order);
    }, 140);
}

void ApiClient::ensureDemoPiles(qint64 stationId)
{
    if (m_demoPilesReady.contains(stationId))
        return;
    const StationSummary *station = demoStationById(stationId);
    if (!station) {
        m_demoPilesReady.insert(stationId);
        return;
    }
    if (station->totalPiles <= 0) {
        m_demoPilesByStation.insert(stationId, {});
        m_demoPilesReady.insert(stationId);
        return;
    }

    int total = qBound(4, station->totalPiles, 12);
    static const char *kStatus[] = {
        "IDLE", "IDLE", "CHARGING", "RESERVED", "FAULT", "OFFLINE", "IDLE"
    };
    QVector<ChargingPile> piles;
    piles.reserve(total);
    for (int i = 0; i < total; ++i) {
        ChargingPile p;
        p.id = stationId * 100 + i + 1;
        p.pileNo = QStringLiteral("BJ-%1-%2")
                       .arg(stationId)
                       .arg(i + 1, 4, 10, QLatin1Char('0'));
        p.stationId = stationId;
        p.stationName = station->name;
        const bool slow = (i % 3 == 0);
        p.pileType = slow ? QStringLiteral("SLOW") : QStringLiteral("FAST");
        p.ratedPowerW = slow ? 7000 : ((i % 2 == 0) ? 60000 : 120000);
        p.status = QString::fromLatin1(kStatus[i % 7]);
        p.lastHeartbeatAt = nowUtcIso();
        p.totalChargeCount = 3 + i;
        p.totalChargeDurationSeconds = 1800 * (i + 1);
        piles.push_back(p);
    }
    m_demoPilesByStation.insert(stationId, piles);
    m_demoPilesReady.insert(stationId);
    syncDemoStationCounts(stationId);
}

void ApiClient::seedDemoHistoryIfNeeded()
{
    if (!m_demoOrders.isEmpty())
        return;
    ChargingOrder done;
    done.id = 101;
    done.orderNo = QStringLiteral("CO20260902000001");
    done.userId = m_demoUser.id;
    done.stationId = 1;
    done.stationName = QStringLiteral("市民中心充电站");
    done.pileId = 101;
    done.pileNo = QStringLiteral("BJ-1-0001");
    done.status = QStringLiteral("COMPLETED");
    done.unitPriceCentsPerKwh = 125;
    done.energyWh = 12345;
    done.durationSeconds = 1800;
    done.amountCents = 1543;
    done.createdAt = QStringLiteral("2026-09-01T08:00:00Z");
    done.settledAt = QStringLiteral("2026-09-01T08:40:00Z");
    m_demoOrders.push_back(done);

    ChargingOrder cancelled;
    cancelled.id = 102;
    cancelled.orderNo = QStringLiteral("CO20260902000002");
    cancelled.userId = m_demoUser.id;
    cancelled.stationId = 2;
    cancelled.stationName = QStringLiteral("中关村充电站");
    cancelled.pileNo = QStringLiteral("BJ-2-0003");
    cancelled.status = QStringLiteral("CANCELLED");
    cancelled.cancelReason = QStringLiteral("用户取消预约");
    cancelled.createdAt = QStringLiteral("2026-09-02T07:30:00Z");
    cancelled.cancelledAt = QStringLiteral("2026-09-02T07:40:00Z");
    m_demoOrders.push_back(cancelled);
}

void ApiClient::lazyExpireDemoReservations()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (ChargingOrder &order : m_demoOrders) {
        if (order.status != QLatin1String("RESERVED"))
            continue;
        QDateTime expires = parseIsoUtc(order.reservationExpiresAt);
        if (!expires.isValid() || expires > now)
            continue;
        order.status = QStringLiteral("CANCELLED");
        order.cancelledAt = nowUtcIso();
        order.cancelReason = QStringLiteral("reservation expired");
        order.updatedAt = order.cancelledAt;
        if (ChargingPile *pile = demoPileById(order.pileId)) {
            if (pile->status == QLatin1String("RESERVED"))
                pile->status = QStringLiteral("IDLE");
            syncDemoStationCounts(pile->stationId);
        }
    }
}

void ApiClient::syncDemoStationCounts(qint64 stationId)
{
    int total = 0;
    int idle = 0;
    int online = 0;
    for (const ChargingPile &p : m_demoPilesByStation.value(stationId)) {
        ++total;
        if (p.status == QLatin1String("IDLE"))
            ++idle;
        if (p.status != QLatin1String("OFFLINE"))
            ++online;
    }
    for (StationSummary &s : m_seedStations) {
        if (s.id != stationId)
            continue;
        if (total > 0) {
            s.totalPiles = total;
            s.availablePiles = idle;
            s.onlineRate = 100.0 * online / total;
            s.isBookable = s.priceCentsPerKwh > 0 && idle > 0;
        }
        break;
    }
}

ChargingPile *ApiClient::demoPileById(qint64 pileId)
{
    for (auto it = m_demoPilesByStation.begin(); it != m_demoPilesByStation.end(); ++it) {
        for (ChargingPile &p : it.value()) {
            if (p.id == pileId)
                return &p;
        }
    }
    return nullptr;
}

ChargingOrder *ApiClient::demoOrderById(qint64 orderId)
{
    for (ChargingOrder &o : m_demoOrders) {
        if (o.id == orderId)
            return &o;
    }
    return nullptr;
}

const StationSummary *ApiClient::demoStationById(qint64 stationId) const
{
    for (const StationSummary &s : m_seedStations) {
        if (s.id == stationId)
            return &s;
    }
    return nullptr;
}

bool ApiClient::demoHasActiveOrder() const
{
    for (const ChargingOrder &o : m_demoOrders) {
        if (isActiveOrderStatus(o.status))
            return true;
    }
    return false;
}

ChargingOrder ApiClient::makeDemoOrder(const ChargingPile &pile, const StationSummary &station) const
{
    ChargingOrder order;
    order.id = m_nextDemoOrderId;
    order.orderNo = QStringLiteral("CO%1%2")
                        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd")))
                        .arg(order.id, 6, 10, QLatin1Char('0'));
    order.userId = m_demoUser.id;
    order.stationId = station.id;
    order.stationName = station.name;
    order.pileId = pile.id;
    order.pileNo = pile.pileNo;
    order.status = QStringLiteral("PENDING");
    order.unitPriceCentsPerKwh = station.priceCentsPerKwh;
    order.ratedPowerW = pile.ratedPowerW;
    order.createdAt = nowUtcIso();
    order.updatedAt = order.createdAt;
    return order;
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
