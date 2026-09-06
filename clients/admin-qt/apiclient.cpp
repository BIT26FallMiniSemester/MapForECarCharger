#include "apiclient.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

ApiClient::ApiClient(QObject *parent) : QObject(parent), m_baseUrl("http://127.0.0.1:8000/api/v1") {}
void ApiClient::setBaseUrl(const QString &url) { m_baseUrl = url.trimmed(); while (m_baseUrl.endsWith('/')) m_baseUrl.chop(1); }
void ApiClient::setToken(const QString &token) { m_token = token; }
void ApiClient::get(const QString &path) { send("GET", path, {}); }
void ApiClient::post(const QString &path, const QJsonObject &body) { send("POST", path, body); }
void ApiClient::put(const QString &path, const QJsonObject &body) { send("PUT", path, body); }
void ApiClient::patch(const QString &path, const QJsonObject &body) { send("PATCH", path, body); }
QString ApiClient::baseUrl() const { return m_baseUrl; }

void ApiClient::send(const QString &method, const QString &path, const QJsonObject &body)
{
    QNetworkRequest req{QUrl(m_baseUrl + path)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_token.isEmpty()) req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = nullptr;
    if (method == "GET") reply = m_manager.get(req);
    else if (method == "POST") reply = m_manager.post(req, json);
    else if (method == "PUT") reply = m_manager.put(req, json);
    else reply = m_manager.sendCustomRequest(req, method.toUtf8(), json);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        const QJsonObject payload = doc.isObject() ? doc.object() : QJsonObject();
        const int code = payload.value("code").toInt(reply->error() == QNetworkReply::NoError ? 0 : -1);
        const QString serverMessage = payload.value("message").toString(reply->errorString());
        if (reply->error() == QNetworkReply::NoError && parseError.error == QJsonParseError::NoError && code == 0)
            emit succeeded(path, payload.value("data"), payload);
        else
            emit failed(path, status, code, userMessage(status, code, serverMessage));
        reply->deleteLater();
    });
}

QString ApiClient::userMessage(int httpStatus, int businessCode, const QString &serverMessage)
{
    switch (businessCode) {
    case 10001: return "输入参数格式或范围不正确";
    case 10002: return "请求内容不符合业务要求";
    case 20001: return "登录已失效，请重新登录";
    case 20002: return "当前管理员没有操作权限";
    case 20004: return "管理员账号或密码错误";
    case 30001: return "目标记录不存在或已被停用";
    case 40002: return "当前电桩状态不允许执行该操作";
    case 40006: return "用户存在未完成订单，暂时不能冻结";
    case 40007: return "编号重复，请更换后重试";
    case 50000: return "服务器内部异常，请稍后重试";
    case 50001: return "后端服务暂不可用，请检查服务状态";
    default: break;
    }
    if (httpStatus == 0) return "无法连接后端服务，请检查地址和服务是否启动";
    return serverMessage.isEmpty() ? QString("请求失败（HTTP %1）").arg(httpStatus) : serverMessage;
}
