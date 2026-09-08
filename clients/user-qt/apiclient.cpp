// 把界面路径映射为服务端 action，并把协议响应转换为界面使用的 JSON 结构。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "apiclient.h"

#include "socketclient.h"

#include <QJsonArray>
#include <QUuid>

/// 创建用户端 API 客户端并连接 SocketClient 的成功/失败信号。
ApiClient::ApiClient(QObject *parent) : QObject(parent), m_socket(new SocketClient(this))
{
    connect(m_socket, &SocketClient::succeeded, this,
            [this](const QString &context, const QJsonValue &data, const QJsonObject &) {
        emit requestFinished();
        handleSuccess(context, data);
    });
    connect(m_socket, &SocketClient::failed, this,
            [this](const QString &context, int code, const QString &message) {
        emit requestFinished();
        const QString translated = chineseMessage(code, message);
        emit requestFailed(context, code, translated);
        emit apiFailed(code, translated);
    });
}

/// 设置 API 后端 Socket 地址。
void ApiClient::setBaseUrl(const QString &endpoint) { m_socket->setEndpoint(endpoint); }
/// 预留演示模式开关，保持 API 客户端接口与其他端一致。
void ApiClient::setDemoMode(bool enabled) { Q_UNUSED(enabled); }
/// 设置后续需要认证的请求令牌。
void ApiClient::setToken(const QString &token) { m_token = token; }
/// 清除本地充电站、订单和电桩显示状态。
void ApiClient::clearSession() { m_token.clear(); }

/// 实现 login 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::login(const QString &phone)
{
    send(QStringLiteral("login"), QStringLiteral("auth.user.login"),
         QJsonObject{{QStringLiteral("phone"), phone}}, false);
}

/// 实现 fetchProfile 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::fetchProfile()
{
    send(QStringLiteral("profile"), QStringLiteral("users.me.get"));
}

/// 实现 updateNickname 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::updateNickname(const QString &nickname)
{
    send(QStringLiteral("nickname"), QStringLiteral("users.me.update"),
         QJsonObject{{QStringLiteral("nickname"), nickname.trimmed()}});
}

void ApiClient::uploadAvatar(const QByteArray &content, const QString &contentType)
{
    send(QStringLiteral("avatar:set"), QStringLiteral("users.avatar.set"),
         QJsonObject{{QStringLiteral("content_type"), contentType},
                     {QStringLiteral("content_base64"), QString::fromLatin1(content.toBase64())}});
}

void ApiClient::fetchAvatar(const QString &avatarId)
{
    send(QStringLiteral("avatar:get"), QStringLiteral("users.avatar.get"),
         QJsonObject{{QStringLiteral("avatar_id"), avatarId}});
}

/// 实现 recharge 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::recharge(double amountYuan)
{
    send(QStringLiteral("recharge"), QStringLiteral("wallet.recharges.create"),
         QJsonObject{{QStringLiteral("amount_cents"), yuanToCents(amountYuan)},
                     {QStringLiteral("client_request_id"), QStringLiteral("qt-")
                          + QUuid::createUuid().toString(QUuid::WithoutBraces)}});
}

/// 实现 fetchRechargeRecords 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::fetchRechargeRecords()
{
    send(QStringLiteral("rechargeRecords"), QStringLiteral("wallet.recharges.list"),
         QJsonObject{{QStringLiteral("page"), 1}, {QStringLiteral("page_size"), 20}});
}

/// 实现 fetchNearbyStations 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::fetchNearbyStations(double latitude, double longitude, double radiusKm)
{
    send(QStringLiteral("nearby"), QStringLiteral("stations.nearby"),
         QJsonObject{{QStringLiteral("latitude"), latitude},
                     {QStringLiteral("longitude"), longitude},
                     {QStringLiteral("radius_km"), radiusKm},
                     {QStringLiteral("page"), 1},
                     {QStringLiteral("page_size"), 100}});
}

/// 实现 fetchMapSnapshot 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::fetchMapSnapshot(double latitude, double longitude, int zoom)
{
    send(QStringLiteral("map"),QStringLiteral("map.snapshot"),QJsonObject{{QStringLiteral("latitude"),latitude},{QStringLiteral("longitude"),longitude},{QStringLiteral("zoom"),zoom}});
}

/// 实现 geocode 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::geocode(const QString &address)
{
    send(QStringLiteral("geocode"), QStringLiteral("map.geocode"),
         QJsonObject{{QStringLiteral("address"), address.trimmed()}});
}

/// 实现 fetchStationPiles 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::fetchStationPiles(qint64 stationId)
{
    m_pileBatches[stationId].clear();
    send(QStringLiteral("piles:%1:1").arg(stationId), QStringLiteral("stations.piles.list"),
         QJsonObject{{QStringLiteral("station_id"), stationId},
                     {QStringLiteral("page"), 1}, {QStringLiteral("page_size"), 100}});
}

/// 实现 fetchActiveOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::fetchActiveOrder()
{
    send(QStringLiteral("order:active"), QStringLiteral("orders.active"));
}

/// 实现 fetchOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::fetchOrder(qint64 orderId)
{
    send(QStringLiteral("order:detail"), QStringLiteral("orders.detail"),
         QJsonObject{{QStringLiteral("order_id"), orderId}});
}

/// 实现 createOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::createOrder(qint64 stationId, qint64 pileId)
{
    send(QStringLiteral("order:create"), QStringLiteral("orders.create"),
         QJsonObject{{QStringLiteral("station_id"), stationId},
                     {QStringLiteral("pile_id"), pileId}});
}

/// 实现 reserveOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::reserveOrder(qint64 orderId)
{
    send(QStringLiteral("order:reserve"), QStringLiteral("orders.reserve"),
         QJsonObject{{QStringLiteral("order_id"), orderId}});
}

/// 实现 startOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::startOrder(qint64 orderId)
{
    send(QStringLiteral("order:start"), QStringLiteral("orders.start"),
         QJsonObject{{QStringLiteral("order_id"), orderId}});
}

/// 实现 stopOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::stopOrder(qint64 orderId)
{
    send(QStringLiteral("order:stop"), QStringLiteral("orders.stop"),
         QJsonObject{{QStringLiteral("order_id"), orderId}});
}

/// 实现 settleOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::settleOrder(qint64 orderId)
{
    send(QStringLiteral("order:settle"), QStringLiteral("orders.settle"),
         QJsonObject{{QStringLiteral("order_id"), orderId}});
}

/// 实现 cancelOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::cancelOrder(qint64 orderId)
{
    send(QStringLiteral("order:cancel"), QStringLiteral("orders.cancel"),
         QJsonObject{{QStringLiteral("order_id"), orderId}});
}

/// 实现 planRoute 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ApiClient::planRoute(double fromLatitude, double fromLongitude,
                          double toLatitude, double toLongitude, const QString &mode)
{
    send(QStringLiteral("route"), QStringLiteral("map.route"),
         QJsonObject{{QStringLiteral("from_latitude"), fromLatitude},
                     {QStringLiteral("from_longitude"), fromLongitude},
                     {QStringLiteral("to_latitude"), toLatitude},
                     {QStringLiteral("to_longitude"), toLongitude},
                     {QStringLiteral("mode"), mode}});
}

/// 为 action 生成 request_id，加入待处理上下文并排队发送请求。
void ApiClient::send(const QString &context, const QString &action,
                     const QJsonObject &data, bool withToken)
{
    emit requestStarted();
    m_socket->send(context, action, data, withToken ? m_token : QString());
}

/// 根据请求上下文把 Socket 响应转换为领域信号。
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
    if (context == QStringLiteral("avatar:set")) {
        emit avatarUploaded(data.toObject().value(QStringLiteral("avatar_id")).toString());
        return;
    }
    if (context == QStringLiteral("avatar:get")) {
        emit avatarReady(QByteArray::fromBase64(
            data.toObject().value(QStringLiteral("content_base64")).toString().toLatin1()));
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
        return;
    }
    if(context==QStringLiteral("map")) {
        const auto bytes=QByteArray::fromBase64(data.toObject().value(QStringLiteral("content_base64")).toString().toLatin1());
        if(bytes.isEmpty()){emit requestFailed(context,50000,QStringLiteral("地图图片无效"));return;}emit mapSnapshotReady(bytes);return;
    }
    if (context == QStringLiteral("geocode")) {
        const QJsonObject object = data.toObject();
        emit locationResolved(object.value(QStringLiteral("latitude")).toDouble(),
                              object.value(QStringLiteral("longitude")).toDouble(),
                              object.value(QStringLiteral("formatted_address")).toString());
        return;
    }
    if (context.startsWith(QStringLiteral("piles:"))) {
        const QJsonObject object = data.toObject();
        const qint64 stationId = context.section(QLatin1Char(':'), 1, 1).toLongLong();
        const int page = context.section(QLatin1Char(':'), 2, 2).toInt();
        QVector<ChargingPile> &piles = m_pileBatches[stationId];
        for (const QJsonValue &value : object.value(QStringLiteral("items")).toArray()) {
            ChargingPile pile = parsePile(value.toObject());
            piles.push_back(pile);
        }
        const qint64 total = object.value(QStringLiteral("total")).toInteger();
        if (piles.size() < total) {
            send(QStringLiteral("piles:%1:%2").arg(stationId).arg(page + 1),
                 QStringLiteral("stations.piles.list"),
                 QJsonObject{{QStringLiteral("station_id"), stationId},
                             {QStringLiteral("page"), page + 1},
                             {QStringLiteral("page_size"), 100}});
            return;
        }
        emit stationPilesReady(stationId, piles);
        m_pileBatches.remove(stationId);
        return;
    }
    if (context == QStringLiteral("order:active")) {
        emit activeOrderReady(!data.isNull(), data.isNull() ? ChargingOrder{} : parseOrder(data.toObject()));
        return;
    }
    if (context.startsWith(QStringLiteral("order:"))) {
        QJsonObject object = data.toObject();
        if (context == QStringLiteral("order:settle")) {
            emit balanceChanged(object.value(QStringLiteral("balance_cents")).toInteger());
            object = object.value(QStringLiteral("order")).toObject();
        }
        emit orderReady(context.section(QLatin1Char(':'), 1), parseOrder(object));
        return;
    }
    if (context == QStringLiteral("route")) {
        const QJsonObject object = data.toObject();
        emit routeReady(RouteInfo{object.value(QStringLiteral("distance_meters")).toInteger(),
                                  object.value(QStringLiteral("duration_seconds")).toInteger()});
    }
}

/// 把 JSON 用户对象转换为客户端 User 模型。
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

/// 把 JSON 站点对象转换为带距离和在线率的客户端模型。
StationSummary ApiClient::parseStation(const QJsonObject &object) const
{
    StationSummary station;
    station.id = object.value(QStringLiteral("id")).toInteger();
    station.name = object.value(QStringLiteral("name")).toString();
    station.address = object.value(QStringLiteral("address")).toString();
    station.operatorName = object.value(QStringLiteral("operator_name")).toString();
    station.district = object.value(QStringLiteral("district")).toString();
    station.latitude = object.value(QStringLiteral("latitude")).toDouble();
    station.longitude = object.value(QStringLiteral("longitude")).toDouble();
    station.priceCentsPerKwh = object.value(QStringLiteral("price_cents_per_kwh")).toInt();
    station.status = object.value(QStringLiteral("status")).toString();
    station.totalPiles = object.value(QStringLiteral("total_piles")).toInt();
    station.availablePiles = object.value(QStringLiteral("available_piles")).toInt();
    station.predictedAvailablePiles1h = object.value(QStringLiteral("predicted_available_piles_1h"))
                                             .toInt(station.availablePiles);
    station.fastConnectorCount = object.value(QStringLiteral("fast_connector_count")).toInt();
    station.slowConnectorCount = object.value(QStringLiteral("slow_connector_count")).toInt();
    station.onlineRate = station.totalPiles ? station.availablePiles * 100.0 / station.totalPiles : 0.0;
    station.distanceKm = object.value(QStringLiteral("route_distance_meters")).toDouble() / 1000.0;
    return station;
}

/// 把 JSON 充值记录转换为客户端 RechargeRecord 模型。
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

/// 把 JSON 电桩对象转换为客户端 ChargingPile 模型。
ChargingPile ApiClient::parsePile(const QJsonObject &object) const
{
    ChargingPile pile;
    pile.id = object.value(QStringLiteral("id")).toInteger();
    pile.stationId = object.value(QStringLiteral("station_id")).toInteger();
    pile.pileNo = object.value(QStringLiteral("pile_no")).toString();
    pile.chargeType = object.value(QStringLiteral("charge_type")).toString();
    pile.ratedPowerW = object.value(QStringLiteral("rated_power_w")).toInteger();
    pile.status = object.value(QStringLiteral("status")).toString();
    return pile;
}

/// 把 JSON 订单及嵌套站点/电桩对象转换为客户端订单模型。
ChargingOrder ApiClient::parseOrder(const QJsonObject &object) const
{
    ChargingOrder order;
    order.id = object.value(QStringLiteral("id")).toInteger();
    order.orderNo = object.value(QStringLiteral("order_no")).toString();
    order.status = object.value(QStringLiteral("status")).toString();
    order.priceCentsPerKwh = object.value(QStringLiteral("price_cents_per_kwh")).toInteger();
    order.durationSeconds = object.value(QStringLiteral("duration_seconds")).toInteger();
    order.energyWh = object.value(QStringLiteral("energy_wh")).toInteger();
    order.amountCents = object.value(QStringLiteral("amount_cents")).toInteger();
    order.expiresAt = object.value(QStringLiteral("expires_at")).toString();
    order.estimated = object.value(QStringLiteral("estimated")).toBool();
    const QJsonObject station = object.value(QStringLiteral("station")).toObject();
    order.stationId = station.value(QStringLiteral("id")).toInteger();
    order.stationName = station.value(QStringLiteral("name")).toString();
    const QJsonObject pile = object.value(QStringLiteral("pile")).toObject();
    order.pileId = pile.value(QStringLiteral("id")).toInteger();
    order.pileNo = pile.value(QStringLiteral("pile_no")).toString();
    order.ratedPowerW = pile.value(QStringLiteral("rated_power_w")).toInteger();
    return order;
}

/// 把用户端业务错误码转换为中文提示。
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
