#ifndef APICLIENT_H
#define APICLIENT_H

#include "models.h"

#include <functional>

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QObject>
#include <QSet>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

// 用户端只通过 HTTP/JSON 访问后端，不直接查数据库。
// demoMode=true 时使用内存模拟数据，方便虚拟机里在没有 FastAPI 时先把页面跑通。
class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url);   // 例如 http://127.0.0.1:8000/api/v1
    void setDemoMode(bool enabled);
    void setToken(const QString &token);
    void clearSession();

    bool demoMode() const { return m_demoMode; }
    QString token() const { return m_token; }

    // POST /user/login
    void login(const QString &phone);
    // GET /user/profile
    void fetchProfile();
    // PUT /user/profile  目前只改昵称
    void updateNickname(const QString &nickname);
    // POST /user/avatar  multipart 字段名 avatar
    void uploadAvatar(const QString &filePath);
    // POST /user/recharge  amountYuan 会在内部换成分
    void recharge(double amountYuan);
    // GET /user/recharge-records
    void fetchRechargeRecords();
    // GET /stations/nearby
    void fetchNearbyStations(double latitude, double longitude, double radiusKm,
                             bool availableOnly = false);
    // GET /stations/filter-options
    void fetchStationFilterOptions();
    // GET /stations  关键字 + 精确筛选
    void fetchStations(const QString &keyword,
                       const QString &district,
                       const QString &operatorName,
                       const QString &regionScope = QString(),
                       const QString &locationType = QString(),
                       bool bookableOnly = false);
    // GET /stations/{station_id}
    void fetchStationDetail(qint64 stationId);
    // GET /stations/{station_id}/piles
    void fetchStationPiles(qint64 stationId,
                           const QString &status = QString(),
                           const QString &pileType = QString());
    // GET /piles/{pile_id}
    void fetchPileDetail(qint64 pileId);
    // GET /orders
    void fetchOrders(const QString &status = QString());
    // GET /orders/active
    void fetchActiveOrder();
    // GET /orders/{order_id}
    void fetchOrderDetail(qint64 orderId);
    // POST /orders
    void createOrder(qint64 pileId);
    // POST /orders/{order_id}/reserve
    void reserveOrder(qint64 orderId);
    // POST /orders/{order_id}/start
    void startOrder(qint64 orderId);
    // POST /orders/{order_id}/stop
    void stopOrder(qint64 orderId);
    // POST /orders/{order_id}/settle
    void settleOrder(qint64 orderId);
    // POST /orders/{order_id}/cancel
    void cancelOrder(qint64 orderId, const QString &reason);
    // 仅演示模式：把当前预约立即视为过期，便于测 40009
    void expireReservationForDemo();

signals:
    void requestStarted();
    void requestFinished();
    void loginSucceeded(const QString &token, const User &user, bool isNewUser);
    void profileReady(const User &user);
    void nicknameUpdated(const User &user);
    void avatarUploaded(const User &user);
    void rechargeSucceeded(qint64 balanceAfterCents, const RechargeRecord &record);
    void rechargeRecordsReady(const QVector<RechargeRecord> &records);
    void nearbyStationsReady(const QVector<StationSummary> &stations);
    void stationFilterOptionsReady(const StationFilterOptions &options);
    void stationsSearchReady(const QVector<StationSummary> &stations, int total);
    void stationDetailReady(const StationSummary &station);
    void stationPilesReady(qint64 stationId, const QVector<ChargingPile> &piles);
    void pileDetailReady(const ChargingPile &pile);
    void ordersReady(const QVector<ChargingOrder> &orders);
    void activeOrderReady(bool hasOrder, const ChargingOrder &order);
    void orderCreated(const ChargingOrder &order);
    void orderUpdated(const ChargingOrder &order);
    void orderSettled(const ChargingOrder &order, qint64 balanceCents);
    void apiFailed(int code, const QString &message);

private:
    struct Envelope {
        int httpStatus = 0;
        int code = -1;
        QString message;
        QJsonValue data;
        bool ok() const { return httpStatus >= 200 && httpStatus < 300 && code == 0; }
    };

    QNetworkRequest makeRequest(const QString &path, bool withAuth, bool jsonContent = true) const;
    void get(const QString &path);
    void sendJson(const QString &method, const QString &path, const QJsonObject &body, bool withAuth);
    void handleReply(QNetworkReply *reply);
    Envelope parseEnvelope(QNetworkReply *reply) const;
    User parseUser(const QJsonObject &obj) const;
    StationSummary parseStation(const QJsonObject &obj) const;
    ChargingPile parsePile(const QJsonObject &obj) const;
    RechargeRecord parseRecharge(const QJsonObject &obj) const;
    ChargingOrder parseOrder(const QJsonObject &obj) const;
    StationFilterOptions parseFilterOptions(const QJsonObject &obj) const;
    QVector<StationSummary> parseStationList(const QJsonValue &data, int *total) const;
    QVector<ChargingOrder> parseOrderList(const QJsonValue &data) const;
    QString chineseMessage(int code, const QString &fallback) const;
    QString nowUtcIso() const;

    void demoLogin(const QString &phone);
    void demoNearby(double latitude, double longitude, double radiusKm, bool availableOnly);
    void demoStationFilterOptions();
    void demoSearchStations(const QString &keyword, const QString &district, const QString &operatorName,
                            const QString &regionScope, const QString &locationType, bool bookableOnly);
    void demoStationDetail(qint64 stationId);
    void demoStationPiles(qint64 stationId, const QString &status, const QString &pileType);
    void demoPileDetail(qint64 pileId);
    void demoOrders(const QString &status);
    void demoActiveOrder();
    void demoOrderDetail(qint64 orderId);
    void demoCreateOrder(qint64 pileId);
    void demoReserveOrder(qint64 orderId);
    void demoStartOrder(qint64 orderId);
    void demoStopOrder(qint64 orderId);
    void demoSettleOrder(qint64 orderId);
    void demoCancelOrder(qint64 orderId, const QString &reason);
    void demoUploadAvatar(const QString &filePath);

    void ensureDemoPiles(qint64 stationId);
    void seedDemoHistoryIfNeeded();
    void lazyExpireDemoReservations();
    void syncDemoStationCounts(qint64 stationId);
    ChargingPile *demoPileById(qint64 pileId);
    ChargingOrder *demoOrderById(qint64 orderId);
    const StationSummary *demoStationById(qint64 stationId) const;
    bool demoHasActiveOrder() const;
    bool demoUserWritable(int *code) const;
    void failDemo(int code);
    void succeedLater(const std::function<void()> &fn, int delayMs = 140);
    ChargingOrder makeDemoOrder(const ChargingPile &pile, const StationSummary &station) const;

    double haversineKm(double lat1, double lon1, double lat2, double lon2) const;

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl = QStringLiteral("http://127.0.0.1:8000/api/v1");
    QString m_token;
    bool m_demoMode = true;
    QString m_pendingKind;
    qint64 m_pendingStationId = 0;

    User m_demoUser;
    QVector<RechargeRecord> m_demoRecharges;
    QVector<ChargingOrder> m_demoOrders;
    QVector<StationSummary> m_seedStations;
    QVector<StationSummary> m_seedStationsInitial;
    QHash<qint64, QVector<ChargingPile>> m_demoPilesByStation;
    QSet<qint64> m_demoPilesReady;
    qint64 m_nextDemoOrderId = 200;
};

#endif
