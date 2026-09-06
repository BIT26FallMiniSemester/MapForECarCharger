#ifndef APICLIENT_H
#define APICLIENT_H

#include "models.h"

#include <QJsonObject>
#include <QObject>
#include <QVector>

class SocketClient;

class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &endpoint);
    void setDemoMode(bool enabled);
    void setToken(const QString &token);
    void clearSession();

    bool demoMode() const { return false; }
    QString token() const { return m_token; }

    void login(const QString &phone);
    void fetchProfile();
    void updateNickname(const QString &nickname);
    void recharge(double amountYuan);
    void fetchRechargeRecords();
    void fetchNearbyStations(double latitude, double longitude, double radiusKm);

signals:
    void requestStarted();
    void requestFinished();
    void loginSucceeded(const QString &token, const User &user, bool isNewUser);
    void profileReady(const User &user);
    void nicknameUpdated(const User &user);
    void rechargeSucceeded(qint64 balanceAfterCents, const RechargeRecord &record);
    void rechargeRecordsReady(const QVector<RechargeRecord> &records);
    void nearbyStationsReady(const QVector<StationSummary> &stations);
    void apiFailed(int code, const QString &message);

private:
    void send(const QString &context, const QString &action, const QJsonObject &data = {}, bool withToken = true);
    void handleSuccess(const QString &context, const QJsonValue &data);
    User parseUser(const QJsonObject &object) const;
    StationSummary parseStation(const QJsonObject &object) const;
    RechargeRecord parseRecharge(const QJsonObject &object) const;
    QString chineseMessage(int code, const QString &fallback) const;

    SocketClient *m_socket = nullptr;
    QString m_token;
};

#endif
