#include "analyticsresult.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

QJsonObject readAnalyticsResult(const QString &path, int maxAgeSeconds)
{
    auto unavailable=[](const QString &reason) {
        return QJsonObject{{"available",false},{"reason",reason}};
    };
    if(path.isEmpty()) return unavailable("not_configured");
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return unavailable("missing_result");
    if(file.size()>2*1024*1024) return unavailable("invalid_result");
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    const auto data=document.object();
    const auto snapshot=QDateTime::fromString(data["snapshot_at"].toString(),Qt::ISODateWithMs);
    const auto generated=QDateTime::fromString(data["generated_at"].toString(),Qt::ISODateWithMs);
    const auto engine=data["engine"].toString();
    const auto trend=data["revenue_trend"].toObject();
    const auto ranking=data["station_ranking"].toObject();
    if(error.error!=QJsonParseError::NoError || data["schema_version"].toInt()!=1 ||
       (engine!="hadoop-mapreduce" && engine!="local-mapreduce") ||
       !snapshot.isValid() || !generated.isValid() ||
       trend["items"].toArray().size()!=30 || !ranking["items"].isArray() ||
       !data["total_revenue_cents"].isDouble() || data["run_id"].toString().isEmpty())
        return unavailable("invalid_result");
    const auto age=snapshot.secsTo(QDateTime::currentDateTimeUtc());
    if(age < -60) return unavailable("invalid_result");
    return {{"available",true},{"stale",age>maxAgeSeconds},
            {"age_seconds",qMax<qint64>(0,age)},{"max_age_seconds",maxAgeSeconds},
            {"data",data}};
}
