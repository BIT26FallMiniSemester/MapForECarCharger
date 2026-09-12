// 声明 Qt 客户端共用的异步 TCP Socket 请求、响应和超时机制。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QTcpSocket>

// 创建客户端 Socket 并连接 Qt 的连接、读取、断开和错误信号。
class SocketClient : public QObject
{
    Q_OBJECT

public:
// 创建客户端 Socket 并连接 Qt 的连接、读取、断开和错误信号。
    explicit SocketClient(QObject *parent = nullptr);
    ~SocketClient() override;

// 解析并切换 Qt Socket 服务端的主机和端口。
    void setEndpoint(const QString &endpoint);
// 返回当前配置的服务端地址。
    QString endpoint() const;
    void send(const QString &context, const QString &action,
              const QJsonObject &data = {}, const QString &token = {});

signals:
    void succeeded(const QString &context, const QJsonValue &data,
                   const QJsonObject &payload);
// 实现 failed 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void failed(const QString &context, int code, const QString &message);

private:
// 仅在 Socket 已连接时把待发送帧写入网络。
    void flushOutput();
// 按长度前缀拆包 JSON 响应，并把成功或失败结果分发给调用方。
    void readResponses();
// 清理连接期间尚未完成的请求并统一发送失败通知。
    void failAll(int code, const QString &message);

    QTcpSocket m_socket;
// 实现 QStringLiteral 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 9000;
    QByteArray m_input;
    QByteArray m_output;
    QHash<QString, QString> m_pending;
};
