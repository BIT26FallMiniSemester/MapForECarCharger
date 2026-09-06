#ifndef APICLIENT_H
#define APICLIENT_H

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>

class SocketClient;

class ApiClient : public QObject
{
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);
    void setBaseUrl(const QString &endpoint);
    void setToken(const QString &token);
    void get(const QString &path);
    void post(const QString &path, const QJsonObject &body = {});
    void put(const QString &path, const QJsonObject &body);
    void patch(const QString &path, const QJsonObject &body);
    QString baseUrl() const;
    static QString userMessage(int businessCode, const QString &serverMessage);

signals:
    void succeeded(const QString &path, const QJsonValue &data, const QJsonObject &payload);
    void failed(const QString &path, int transportStatus, int businessCode, const QString &message);

private:
    void send(const QString &method, const QString &path, QJsonObject body);
    QJsonValue adapt(const QString &action, const QJsonValue &data) const;

    SocketClient *m_socket = nullptr;
    QString m_token;
};
#endif
