// 声明客户端 HTTP 风格 API 到 Qt Socket action 的映射接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef APICLIENT_H
#define APICLIENT_H

#include "models.h"

#include <QJsonObject>
#include <QObject>
#include <QVector>

// 创建客户端 Socket 并连接 Qt 的连接、读取、断开和错误信号。
class SocketClient;

// 创建用户端 API 客户端并连接 SocketClient 的成功/失败信号。
class ApiClient : public QObject
{
    Q_OBJECT

public:
// 创建用户端 API 客户端并连接 SocketClient 的成功/失败信号。
    explicit ApiClient(QObject *parent = nullptr);

// 设置 API 后端 Socket 地址。
    void setBaseUrl(const QString &endpoint);
// 预留演示模式开关，保持 API 客户端接口与其他端一致。
    void setDemoMode(bool enabled);
// 设置后续需要认证的请求令牌。
    void setToken(const QString &token);
// 清除本地充电站、订单和电桩显示状态。
    void clearSession();

    bool demoMode() const { return false; }
    QString token() const { return m_token; }

// 实现 login 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void login(const QString &phone);
// 实现 fetchProfile 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void fetchProfile();
// 实现 updateNickname 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void updateNickname(const QString &nickname);
// 实现 recharge 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void recharge(double amountYuan);
// 实现 fetchRechargeRecords 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void fetchRechargeRecords();
// 实现 fetchNearbyStations 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void fetchNearbyStations(double latitude, double longitude, double radiusKm);
// 实现 fetchMapSnapshot 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void fetchMapSnapshot(double latitude, double longitude, int zoom = 14);
// 实现 geocode 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void geocode(const QString &address);
// 实现 fetchStationPiles 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void fetchStationPiles(qint64 stationId);
// 实现 fetchActiveOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void fetchActiveOrder();
// 实现 fetchOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void fetchOrder(qint64 orderId);
// 实现 createOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void createOrder(qint64 stationId, qint64 pileId);
// 实现 reserveOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void reserveOrder(qint64 orderId);
// 实现 startOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void startOrder(qint64 orderId);
// 实现 stopOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void stopOrder(qint64 orderId);
// 实现 settleOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void settleOrder(qint64 orderId);
// 实现 cancelOrder 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void cancelOrder(qint64 orderId);
    void planRoute(double fromLatitude, double fromLongitude,
                   double toLatitude, double toLongitude);

signals:
// 实现 requestStarted 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void requestStarted();
// 实现 requestFinished 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void requestFinished();
// 实现 loginSucceeded 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void loginSucceeded(const QString &token, const User &user, bool isNewUser);
// 实现 profileReady 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void profileReady(const User &user);
// 实现 nicknameUpdated 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void nicknameUpdated(const User &user);
// 实现 rechargeSucceeded 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void rechargeSucceeded(qint64 balanceAfterCents, const RechargeRecord &record);
// 实现 rechargeRecordsReady 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void rechargeRecordsReady(const QVector<RechargeRecord> &records);
// 实现 nearbyStationsReady 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void nearbyStationsReady(const QVector<StationSummary> &stations);
// 实现 mapSnapshotReady 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void mapSnapshotReady(const QByteArray &png);
// 实现 locationResolved 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void locationResolved(double latitude, double longitude, const QString &displayName);
// 实现 stationPilesReady 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void stationPilesReady(qint64 stationId, const QVector<ChargingPile> &piles);
// 实现 activeOrderReady 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void activeOrderReady(bool hasOrder, const ChargingOrder &order);
// 实现 orderReady 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void orderReady(const QString &operation, const ChargingOrder &order);
// 实现 routeReady 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void routeReady(const RouteInfo &route);
// 实现 balanceChanged 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void balanceChanged(qint64 balanceCents);
// 实现 requestFailed 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void requestFailed(const QString &context, int code, const QString &message);
// 实现 apiFailed 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void apiFailed(int code, const QString &message);

private:
    void send(const QString &context, const QString &action, const QJsonObject &data = {}, bool withToken = true);
// 根据请求上下文把 Socket 响应转换为领域信号。
    void handleSuccess(const QString &context, const QJsonValue &data);
// 把 JSON 用户对象转换为客户端 User 模型。
    User parseUser(const QJsonObject &object) const;
// 把 JSON 站点对象转换为带距离和在线率的客户端模型。
    StationSummary parseStation(const QJsonObject &object) const;
// 把 JSON 充值记录转换为客户端 RechargeRecord 模型。
    RechargeRecord parseRecharge(const QJsonObject &object) const;
// 把 JSON 电桩对象转换为客户端 ChargingPile 模型。
    ChargingPile parsePile(const QJsonObject &object) const;
// 把 JSON 订单及嵌套站点/电桩对象转换为客户端订单模型。
    ChargingOrder parseOrder(const QJsonObject &object) const;
// 把用户端业务错误码转换为中文提示。
    QString chineseMessage(int code, const QString &fallback) const;

    SocketClient *m_socket = nullptr;
    QString m_token;
};

#endif
