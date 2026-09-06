#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QTcpSocket>

class SocketClient : public QObject
{
    Q_OBJECT

public:
    explicit SocketClient(QObject *parent = nullptr);

    void setEndpoint(const QString &endpoint);
    QString endpoint() const;
    void send(const QString &context, const QString &action,
              const QJsonObject &data = {}, const QString &token = {});

signals:
    void succeeded(const QString &context, const QJsonValue &data,
                   const QJsonObject &payload);
    void failed(const QString &context, int code, const QString &message);

private:
    void readResponses();
    void failAll(int code, const QString &message);

    QTcpSocket m_socket;
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 9000;
    QByteArray m_input;
    QByteArray m_output;
    QHash<QString, QString> m_pending;
};
