#ifndef APICLIENT_H
#define APICLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QJsonValue>

class ApiClient : public QObject
{
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);
    void setBaseUrl(const QString &url);
    void setToken(const QString &token);
    void get(const QString &path);
    void post(const QString &path, const QJsonObject &body = {});
    void put(const QString &path, const QJsonObject &body);
    void patch(const QString &path, const QJsonObject &body);
    QString baseUrl() const;
    static QString userMessage(int httpStatus, int businessCode, const QString &serverMessage);
signals:
    void succeeded(const QString &path, const QJsonValue &data, const QJsonObject &payload);
    void failed(const QString &path, int httpStatus, int businessCode, const QString &message);
private:
    void send(const QString &method, const QString &path, const QJsonObject &body);
    QNetworkAccessManager m_manager;
    QString m_baseUrl;
    QString m_token;
};
#endif
