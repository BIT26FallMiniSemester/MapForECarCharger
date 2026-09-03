#ifndef APICLIENT_H
#define APICLIENT_H

#include "models.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QObject>
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
    // POST /user/recharge  amountYuan 会在内部换成分
    void recharge(double amountYuan);
    // GET /user/recharge-records
    void fetchRechargeRecords();
    // GET /stations/nearby
    void fetchNearbyStations(double latitude, double longitude, double radiusKm);
    // GET /stations/{station_id}/piles
    void fetchStationPiles(qint64 stationId);
    // GET /orders
    void fetchOrders();

signals:
    void requestStarted();
    void requestFinished();
    void loginSucceeded(const QString &token, const User &user, bool isNewUser);
    void profileReady(const User &user);
    void nicknameUpdated(const User &user);
    void rechargeSucceeded(qint64 balanceAfterCents, const RechargeRecord &record);
    void rechargeRecordsReady(const QVector<RechargeRecord> &records);
    void nearbyStationsReady(const QVector<StationSummary> &stations);
    void stationPilesReady(qint64 stationId, const QVector<ChargingPile> &piles);
    void ordersReady(const QVector<ChargingOrder> &orders);
    void apiFailed(int code, const QString &message);

private:
    struct Envelope {
        int httpStatus = 0;
        int code = -1;
        QString message;
        QJsonValue data;
        bool ok() const { return httpStatus >= 200 && httpStatus < 300 && code == 0; }
    };

    QNetworkRequest makeRequest(const QString &path, bool withAuth) const;
    void get(const QString &path);
    void sendJson(const QString &method, const QString &path, const QJsonObject &body, bool withAuth);
    void handleReply(QNetworkReply *reply);
    Envelope parseEnvelope(QNetworkReply *reply) const;
    User parseUser(const QJsonObject &obj) const;
    StationSummary parseStation(const QJsonObject &obj) const;
    ChargingPile parsePile(const QJsonObject &obj) const;
    RechargeRecord parseRecharge(const QJsonObject &obj) const;
    ChargingOrder parseOrder(const QJsonObject &obj) const;
    QString chineseMessage(int code, const QString &fallback) const;

    void demoLogin(const QString &phone);
    void demoNearby(double latitude, double longitude, double radiusKm);
    void demoStationPiles(qint64 stationId);
    void demoOrders();
    double haversineKm(double lat1, double lon1, double lat2, double lon2) const;

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl = QStringLiteral("http://127.0.0.1:8000/api/v1");
    QString m_token;
    bool m_demoMode = true;
    QString m_pendingKind;   // 用来区分同一套回调对应哪个接口
    qint64 m_pendingStationId = 0;

    // 演示模式内存数据（进程内有效）
    User m_demoUser;
    QVector<RechargeRecord> m_demoRecharges;
    QVector<ChargingOrder> m_demoOrders;
    QVector<StationSummary> m_seedStations;
};

#endif
