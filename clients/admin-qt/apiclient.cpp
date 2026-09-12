// 把界面路径映射为服务端 action，并把协议响应转换为界面使用的 JSON 结构。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "apiclient.h"

#include "socketclient.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {
void addQuery(QJsonObject &data, const QUrlQuery &query, const QString &source,
              const QString &target, bool integer = false)
{
    const QString value = query.queryItemValue(source);
    if (!value.isEmpty())
        data.insert(target, integer ? QJsonValue(value.toInt()) : QJsonValue(value));
}

QJsonObject pagination(QJsonObject object)
{
    const int page = object.value(QStringLiteral("page")).toInt(1);
    const int pageSize = object.value(QStringLiteral("page_size")).toInt(20);
    const int total = object.value(QStringLiteral("total")).toInt();
    object.insert(QStringLiteral("pagination"),
                  QJsonObject{{QStringLiteral("page"), page},
                              {QStringLiteral("total"), total},
                              {QStringLiteral("total_pages"), qMax(1, (total + pageSize - 1) / pageSize)}});
    return object;
}

QJsonObject adaptPile(QJsonObject pile)
{
    pile.insert(QStringLiteral("pile_type"), pile.value(QStringLiteral("charge_type")));
    pile.insert(QStringLiteral("last_heartbeat_at"), QStringLiteral("未接入"));
    return pile;
}
}

/// 创建用户端 API 客户端并连接 SocketClient 的成功/失败信号。
ApiClient::ApiClient(QObject *parent) : QObject(parent), m_socket(new SocketClient(this))
{
    connect(m_socket, &SocketClient::succeeded, this,
            [this](const QString &path, const QJsonValue &data, const QJsonObject &payload) {
        emit succeeded(path, adapt(payload.value(QStringLiteral("action")).toString(), data), payload);
    });
    connect(m_socket, &SocketClient::failed, this,
            [this](const QString &path, int code, const QString &message) {
        emit failed(path, 0, code, userMessage(code, message));
    });
}

/// 设置 API 后端 Socket 地址。
void ApiClient::setBaseUrl(const QString &endpoint) { m_socket->setEndpoint(endpoint); }
/// 设置后续需要认证的请求令牌。
void ApiClient::setToken(const QString &token) { m_token = token; }
/// 发起 GET 风格请求。
void ApiClient::get(const QString &path) { send(QStringLiteral("GET"), path, {}); }
/// 发起 POST 风格请求。
void ApiClient::post(const QString &path, const QJsonObject &body) { send(QStringLiteral("POST"), path, body); }
/// 发起 PUT 风格请求。
void ApiClient::put(const QString &path, const QJsonObject &body) { send(QStringLiteral("PUT"), path, body); }
/// 发起 PATCH 风格请求。
void ApiClient::patch(const QString &path, const QJsonObject &body) { send(QStringLiteral("PATCH"), path, body); }
/// 实现 baseUrl 的本地处理逻辑，保持与项目其他模块的接口约定一致。
QString ApiClient::baseUrl() const { return m_socket->endpoint(); }

/// 为 action 生成 request_id，加入待处理上下文并排队发送请求。
void ApiClient::send(const QString &method, const QString &path, QJsonObject body)
{
    const QUrl url(QStringLiteral("tcp://local") + path);
    const QString route = url.path();
    const QUrlQuery query(url);
    QString action;
    QRegularExpressionMatch match;

    if (route == QStringLiteral("/admin/login"))
        action = QStringLiteral("auth.admin.login");
    else if (route == QStringLiteral("/admin/overview"))
        action = QStringLiteral("admin.overview");
/// 实现 if 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    else if (route == QStringLiteral("/admin/revenue-trend")) {
        action = QStringLiteral("admin.revenue_trend");
        addQuery(body, query, QStringLiteral("days"), QStringLiteral("days"), true);
    } else if (route == QStringLiteral("/dashboard/pile-status"))
        action = QStringLiteral("admin.pile_status");
/// 实现 if 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    else if (route == QStringLiteral("/admin/piles")) {
        action = QStringLiteral("admin.piles.list");
        for (const QString &field : {QStringLiteral("page"), QStringLiteral("page_size"), QStringLiteral("station_id")})
            addQuery(body, query, field, field, true);
        addQuery(body, query, QStringLiteral("status"), QStringLiteral("status"));
        addQuery(body, query, QStringLiteral("keyword"), QStringLiteral("keyword"));
    } else if (route == QStringLiteral("/admin/stations")) {
        action = method == QStringLiteral("GET") ? QStringLiteral("admin.stations.list")
                                                  : QStringLiteral("admin.stations.create");
        addQuery(body, query, QStringLiteral("page"), QStringLiteral("page"), true);
        addQuery(body, query, QStringLiteral("page_size"), QStringLiteral("page_size"), true);
    } else if (route == QStringLiteral("/admin/users")) {
        action = QStringLiteral("admin.users.list");
        addQuery(body, query, QStringLiteral("page"), QStringLiteral("page"), true);
        addQuery(body, query, QStringLiteral("page_size"), QStringLiteral("page_size"), true);
        addQuery(body, query, QStringLiteral("phone_keyword"), QStringLiteral("keyword"));
    } else if (route == QStringLiteral("/admin/orders")) {
        action = QStringLiteral("admin.orders.list");
        addQuery(body, query, QStringLiteral("page"), QStringLiteral("page"), true);
        addQuery(body, query, QStringLiteral("page_size"), QStringLiteral("page_size"), true);
        addQuery(body, query, QStringLiteral("status"), QStringLiteral("status"));
        addQuery(body, query, QStringLiteral("keyword"), QStringLiteral("keyword"));
    } else if ((match = QRegularExpression(QStringLiteral("^/admin/piles/(\\d+)/restart$")).match(route)).hasMatch()) {
        action = QStringLiteral("admin.piles.recover");
        body.insert(QStringLiteral("pile_id"), match.captured(1).toInt());
    } else if ((match = QRegularExpression(QStringLiteral("^/admin/piles/(\\d+)$")).match(route)).hasMatch()) {
        action = QStringLiteral("admin.piles.detail");
        body.insert(QStringLiteral("pile_id"), match.captured(1).toInt());
    } else if ((match = QRegularExpression(QStringLiteral("^/admin/stations/(\\d+)/piles$")).match(route)).hasMatch()) {
        action = QStringLiteral("admin.piles.create");
        body.insert(QStringLiteral("station_id"), match.captured(1).toInt());
        body.insert(QStringLiteral("charge_type"), body.take(QStringLiteral("pile_type")));
    } else if ((match = QRegularExpression(QStringLiteral("^/admin/stations/(\\d+)$")).match(route)).hasMatch()) {
        action = QStringLiteral("admin.stations.update");
        body.insert(QStringLiteral("station_id"), match.captured(1).toInt());
    } else if ((match = QRegularExpression(QStringLiteral("^/admin/users/(\\d+)/(freeze|unfreeze)$")).match(route)).hasMatch()) {
        action = QStringLiteral("admin.users.") + match.captured(2);
        body.insert(QStringLiteral("user_id"), match.captured(1).toInt());
    } else if ((match = QRegularExpression(QStringLiteral("^/admin/users/(\\d+)$")).match(route)).hasMatch()) {
        action = QStringLiteral("admin.users.detail");
        body.insert(QStringLiteral("user_id"), match.captured(1).toInt());
    }

    if (action.isEmpty()) {
        emit failed(path, 0, 40009, QStringLiteral("客户端尚未映射该 Qt Socket 操作"));
        return;
    }
    m_socket->send(path, action, body, action.startsWith(QStringLiteral("auth.")) ? QString() : m_token);
}

/// 按 action 对服务端响应做列表、分页和展示字段适配。
QJsonValue ApiClient::adapt(const QString &action, const QJsonValue &data) const
{
    if (action == QStringLiteral("admin.pile_status")) {
        QJsonArray items;
        const QJsonObject counts = data.toObject();
        for (const QString &status : {QStringLiteral("IDLE"), QStringLiteral("RESERVED"),
                                      QStringLiteral("CHARGING"), QStringLiteral("FAULT"),
                                      QStringLiteral("OFFLINE")})
            items.append(QJsonObject{{QStringLiteral("status"), status},
                                     {QStringLiteral("count"), counts.value(status)}});
        return QJsonObject{{QStringLiteral("items"), items}};
    }
    if (action == QStringLiteral("admin.overview")) {
        QJsonObject object = data.toObject();
        if (!object.contains(QStringLiteral("total_energy_wh")))
            object.insert(QStringLiteral("total_energy_wh"), object.value(QStringLiteral("today_energy_wh")));
        return object;
    }
    if (action.endsWith(QStringLiteral(".list"))) {
        QJsonObject object = pagination(data.toObject());
        QJsonArray items = object.value(QStringLiteral("items")).toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items[i].toObject();
            if (action == QStringLiteral("admin.piles.list"))
                item = adaptPile(item);
/// 实现 if 的本地处理逻辑，保持与项目其他模块的接口约定一致。
            else if (action == QStringLiteral("admin.stations.list")) {
                const int total = item.value(QStringLiteral("total_piles")).toInt();
                const int online = item.value(QStringLiteral("online_piles")).toInt();
                item.insert(QStringLiteral("online_rate"), total ? online * 100.0 / total : 0.0);
            }
            items[i] = item;
        }
        object.insert(QStringLiteral("items"), items);
        return object;
    }
    if (action == QStringLiteral("admin.piles.detail"))
        return adaptPile(data.toObject());
    return data;
}

/// 把服务端业务错误码转换为管理端可读的中文提示。
QString ApiClient::userMessage(int businessCode, const QString &serverMessage)
{
    switch (businessCode) {
    case 40001: return QStringLiteral("输入参数格式或范围不正确");
    case 40002: return QStringLiteral("当前用户已有未完成订单");
    case 40003: return QStringLiteral("站点或电桩当前不可用");
    case 40004: return QStringLiteral("当前状态不允许执行该操作");
    case 40007: return QStringLiteral("用户存在未完成订单，暂时不能冻结");
    case 40008: return QStringLiteral("编号重复，请更换后重试");
    case 40101: return QStringLiteral("管理员账号、密码或登录会话无效");
    case 40301: return QStringLiteral("当前管理员没有操作权限");
    case 40401: return QStringLiteral("目标记录不存在");
    case 50001: return serverMessage;
    default: break;
    }
    return serverMessage.isEmpty() ? QStringLiteral("Qt 后端请求失败") : serverMessage;
}
