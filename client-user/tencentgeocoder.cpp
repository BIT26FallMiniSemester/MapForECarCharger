#include "tencentgeocoder.h"

#include "mapconfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

TencentGeocoder::TencentGeocoder(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_key = tencentMapKey();
}

void TencentGeocoder::setApiKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    m_key = trimmed.isEmpty() ? tencentMapKey() : trimmed;
}

void TencentGeocoder::get(const QUrl &url)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(12000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

void TencentGeocoder::locateByIp()
{
    m_pending = QStringLiteral("ip");
    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/location/v1/ip"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), m_key);
    url.setQuery(query);
    get(url);
}

void TencentGeocoder::searchPlace(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        emit geocodeFailed(QStringLiteral("请输入要搜索的位置"));
        return;
    }
    if (m_key.isEmpty() && tryLocalMock(trimmed))
        return;

    m_pendingKeyword = trimmed;
    m_pending = QStringLiteral("suggestion");
    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/place/v1/suggestion"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("keyword"), trimmed);
    query.addQueryItem(QStringLiteral("region"), QStringLiteral("北京"));
    query.addQueryItem(QStringLiteral("key"), m_key);
    url.setQuery(query);
    get(url);
}

void TencentGeocoder::reverseGeocode(double latitude, double longitude)
{
    m_pending = QStringLiteral("geocode");
    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("location"),
                       QStringLiteral("%1,%2").arg(latitude, 0, 'f', 6).arg(longitude, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("key"), m_key);
    url.setQuery(query);
    get(url);
}

void TencentGeocoder::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit geocodeFailed(QStringLiteral("腾讯地图请求失败，请检查网络和 Key 配额"));
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    if (root.value(QStringLiteral("status")).toInt(-1) != 0) {
        if (m_pending == QLatin1String("suggestion") && !m_pendingKeyword.isEmpty()) {
            m_pending = QStringLiteral("geocode");
            QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("address"), m_pendingKeyword);
            query.addQueryItem(QStringLiteral("key"), m_key);
            url.setQuery(query);
            get(url);
            return;
        }
        emit geocodeFailed(root.value(QStringLiteral("message")).toString(QStringLiteral("定位失败")));
        return;
    }

    if (m_pending == QLatin1String("ip"))
        parseIp(root);
    else if (m_pending == QLatin1String("suggestion"))
        parseSuggestion(root);
    else
        parseGeocoder(root);
}

void TencentGeocoder::parseIp(const QJsonObject &root)
{
    const QJsonObject result = root.value(QStringLiteral("result")).toObject();
    const QJsonObject loc = result.value(QStringLiteral("location")).toObject();
    const double lat = loc.value(QStringLiteral("lat")).toDouble();
    const double lng = loc.value(QStringLiteral("lng")).toDouble();
    if (lat == 0.0 && lng == 0.0) {
        emit geocodeFailed(QStringLiteral("IP 定位没有返回坐标"));
        return;
    }

    const QJsonObject ad = result.value(QStringLiteral("ad_info")).toObject();
    QStringList parts;
    const QString province = ad.value(QStringLiteral("province")).toString();
    const QString city = ad.value(QStringLiteral("city")).toString();
    const QString district = ad.value(QStringLiteral("district")).toString();
    if (!province.isEmpty())
        parts << province;
    if (!city.isEmpty() && city != province)
        parts << city;
    if (!district.isEmpty())
        parts << district;
    QString name = parts.join(QString());
    if (name.isEmpty())
        name = QStringLiteral("当前位置");
    name += QStringLiteral("（当前定位）");
    emit geocodeSucceeded(lat, lng, name);
}

void TencentGeocoder::parseGeocoder(const QJsonObject &root)
{
    const QJsonObject result = root.value(QStringLiteral("result")).toObject();
    const QJsonObject loc = result.value(QStringLiteral("location")).toObject();
    const double lat = loc.value(QStringLiteral("lat")).toDouble();
    const double lng = loc.value(QStringLiteral("lng")).toDouble();

    QString name = result.value(QStringLiteral("title")).toString();
    if (name.isEmpty())
        name = result.value(QStringLiteral("address")).toString();
    const QJsonObject formatted = result.value(QStringLiteral("formatted_addresses")).toObject();
    if (name.isEmpty())
        name = formatted.value(QStringLiteral("recommend")).toString();
    if (name.isEmpty())
        name = QStringLiteral("已定位");
    emit geocodeSucceeded(lat, lng, name);
}

void TencentGeocoder::parseSuggestion(const QJsonObject &root)
{
    const QJsonArray data = root.value(QStringLiteral("data")).toArray();
    if (data.isEmpty()) {
        m_pending = QStringLiteral("geocode");
        QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("address"), m_pendingKeyword);
        query.addQueryItem(QStringLiteral("key"), m_key);
        url.setQuery(query);
        get(url);
        return;
    }

    const QJsonObject item = data.first().toObject();
    const QJsonObject loc = item.value(QStringLiteral("location")).toObject();
    QString name = item.value(QStringLiteral("title")).toString();
    const QString address = item.value(QStringLiteral("address")).toString();
    if (!address.isEmpty() && !name.contains(address))
        name = name + QStringLiteral(" · ") + address;
    emit geocodeSucceeded(loc.value(QStringLiteral("lat")).toDouble(),
                          loc.value(QStringLiteral("lng")).toDouble(),
                          name);
}

bool TencentGeocoder::tryLocalMock(const QString &address)
{
    struct Mock {
        const char *keyword;
        double lat;
        double lng;
        const char *name;
    };
    static const Mock kMocks[] = {
        {"天安门", 39.9042, 116.4074, "北京市东城区天安门"},
        {"市民中心", 39.9050, 116.4000, "北京市东城区市民中心"},
        {"中关村", 39.9836, 116.3159, "北京市海淀区中关村"},
        {"望京", 39.9960, 116.4700, "北京市朝阳区望京"},
        {"北京南站", 39.8650, 116.3780, "北京市丰台区北京南站"},
        {"科技园", 39.9800, 116.3100, "北京市海淀区科技园"},
    };

    for (const Mock &item : kMocks) {
        if (address.contains(QString::fromUtf8(item.keyword))) {
            emit geocodeSucceeded(item.lat, item.lng, QString::fromUtf8(item.name));
            return true;
        }
    }
    return false;
}
