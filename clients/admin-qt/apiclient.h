// 声明客户端 HTTP 风格 API 到 Qt Socket action 的映射接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef APICLIENT_H
#define APICLIENT_H

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>

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
// 设置后续需要认证的请求令牌。
    void setToken(const QString &token);
// 发起 GET 风格请求。
    void get(const QString &path);
    void post(const QString &path, const QJsonObject &body = {});
// 发起 PUT 风格请求。
    void put(const QString &path, const QJsonObject &body);
// 发起 PATCH 风格请求。
    void patch(const QString &path, const QJsonObject &body);
// 实现 baseUrl 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    QString baseUrl() const;
// 把服务端业务错误码转换为管理端可读的中文提示。
    static QString userMessage(int businessCode, const QString &serverMessage);

signals:
// 实现 succeeded 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void succeeded(const QString &path, const QJsonValue &data, const QJsonObject &payload);
// 实现 failed 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void failed(const QString &path, int transportStatus, int businessCode, const QString &message);

private:
// 为 action 生成 request_id，加入待处理上下文并排队发送请求。
    void send(const QString &method, const QString &path, QJsonObject body);
// 按 action 对服务端响应做列表、分页和展示字段适配。
    QJsonValue adapt(const QString &action, const QJsonValue &data) const;

    SocketClient *m_socket = nullptr;
    QString m_token;
};
#endif
