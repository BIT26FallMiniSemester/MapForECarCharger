// 实现 SocketClient 的连接、请求排队、长度帧编码、响应解析和失败清理。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "socketclient.h"

#include <QJsonDocument>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QtEndian>

namespace {
QByteArray frame(const QJsonObject &object)
{
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray bytes(4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(json.size()), bytes.data());
    return bytes + json;
}
}

/// 创建客户端 Socket 并连接 Qt 的连接、读取、断开和错误信号。
SocketClient::SocketClient(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, &SocketClient::flushOutput);
    connect(&m_socket, &QTcpSocket::bytesWritten, this, [this](qint64) { flushOutput(); });
    connect(&m_socket, &QTcpSocket::readyRead, this, &SocketClient::readResponses);
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        if (!m_pending.isEmpty())
            failAll(50001, QStringLiteral("与 Qt 后端的连接已断开"));
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_pending.isEmpty() && m_socket.state() == QAbstractSocket::UnconnectedState)
            failAll(50001, QStringLiteral("无法连接 Qt 后端：") + m_socket.errorString());
    });
}

/// 解析并切换 Qt Socket 服务端的主机和端口。
void SocketClient::setEndpoint(const QString &endpoint)
{
    QString value = endpoint.trimmed();
    if (!value.contains(QStringLiteral("://")))
        value.prepend(QStringLiteral("tcp://"));
    const QUrl url(value);
    const QString host = url.host().trimmed();
    const int port = url.port(9000);
    if (host.isEmpty() || port < 1 || port > 65535)
        return;
    if (host == m_host && port == m_port)
        return;
    m_socket.abort();
    m_host = host;
    m_port = quint16(port);
}

/// 返回当前配置的服务端地址。
QString SocketClient::endpoint() const
{
    return QStringLiteral("%1:%2").arg(m_host).arg(m_port);
}

/// 为 action 生成 request_id，加入待处理上下文并排队发送请求。
void SocketClient::send(const QString &context, const QString &action,
                        const QJsonObject &data, const QString &token)
{
    const QString requestId = QStringLiteral("qt_")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject request{{QStringLiteral("version"), 1},
                        {QStringLiteral("request_id"), requestId},
                        {QStringLiteral("action"), action},
                        {QStringLiteral("data"), data}};
    if (!token.isEmpty())
        request.insert(QStringLiteral("token"), token);

    m_pending.insert(requestId, context);
    m_output += frame(request);
    if (m_socket.state() == QAbstractSocket::ConnectedState)
        flushOutput();
    else if (m_socket.state() == QAbstractSocket::UnconnectedState)
        m_socket.connectToHost(m_host, m_port);

/// 实现 singleShot 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    QTimer::singleShot(15000, this, [this, requestId] {
        const auto it = m_pending.find(requestId);
        if (it == m_pending.end())
            return;
        const QString context = it.value();
        m_pending.erase(it);
        emit failed(context, 50001, QStringLiteral("Qt 后端请求超时"));
    });
}

/// 仅在 Socket 已连接时把待发送帧写入网络。
void SocketClient::flushOutput()
{
    if (m_socket.state() != QAbstractSocket::ConnectedState || m_output.isEmpty())
        return;
    const qint64 accepted = m_socket.write(m_output);
    if (accepted > 0)
        m_output.remove(0, int(accepted));
}

/// 按长度前缀拆包 JSON 响应，并把成功或失败结果分发给调用方。
void SocketClient::readResponses()
{
    m_input += m_socket.readAll();
    while (m_input.size() >= 4) {
        const quint32 length = qFromBigEndian<quint32>(m_input.constData());
        if (length == 0 || length > 1048576) {
            m_socket.abort();
            failAll(50000, QStringLiteral("Qt 后端返回了非法数据帧"));
            return;
        }
        if (m_input.size() < int(length) + 4)
            return;

        const QJsonDocument document = QJsonDocument::fromJson(m_input.mid(4, length));
        m_input.remove(0, int(length) + 4);
        if (!document.isObject()) {
            m_socket.abort();
            failAll(50000, QStringLiteral("Qt 后端返回了无法解析的数据"));
            return;
        }

        const QJsonObject payload = document.object();
        const QString requestId = payload.value(QStringLiteral("request_id")).toString();
        const auto it = m_pending.find(requestId);
        if (it == m_pending.end())
            continue;
        const QString context = it.value();
        m_pending.erase(it);
        const int code = payload.value(QStringLiteral("code")).toInt(-1);
        if (code == 0)
            emit succeeded(context, payload.value(QStringLiteral("data")), payload);
        else
            emit failed(context, code,
                        payload.value(QStringLiteral("message")).toString(QStringLiteral("请求失败")));
    }
}

/// 清理连接期间尚未完成的请求并统一发送失败通知。
void SocketClient::failAll(int code, const QString &message)
{
    const auto contexts = m_pending.values();
    m_pending.clear();
    m_input.clear();
    m_output.clear();
    for (const QString &context : contexts)
        emit failed(context, code, message);
}
