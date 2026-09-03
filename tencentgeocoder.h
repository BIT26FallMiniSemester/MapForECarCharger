#ifndef TENCENTGEOCODER_H
#define TENCENTGEOCODER_H

#include <QJsonObject>
#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

class TencentGeocoder : public QObject
{
    Q_OBJECT

public:
    explicit TencentGeocoder(QObject *parent = nullptr);

    void setApiKey(const QString &key);
    QString apiKey() const { return m_key; }

    // 按 IP 粗略定位（虚拟机没有 GPS，这是「定位我自己」）
    void locateByIp();
    // 搜索地点：先走地点提示，失败再走地址解析
    void searchPlace(const QString &keyword);
    void reverseGeocode(double latitude, double longitude);

signals:
    void geocodeSucceeded(double latitude, double longitude, const QString &displayName);
    void geocodeFailed(const QString &message);

private:
    void get(const QUrl &url);
    void handleReply(QNetworkReply *reply);
    void parseIp(const QJsonObject &root);
    void parseGeocoder(const QJsonObject &root);
    void parseSuggestion(const QJsonObject &root);
    bool tryLocalMock(const QString &address);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_key;
    QString m_pending;
    QString m_pendingKeyword;
};

#endif
