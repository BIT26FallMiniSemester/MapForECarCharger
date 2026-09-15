#include "analyticsresult.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <cmath>

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

QJsonObject readForecastResult(const QString &path, int maxAgeSeconds)
{
    auto unavailable=[](const QString &reason) {
        return QJsonObject{{"available",false},{"reason",reason}};
    };
    if(path.isEmpty()) return unavailable("not_configured");
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return unavailable("missing_result");
    if(file.size()>32*1024*1024) return unavailable("invalid_result");
    QJsonParseError error;
    const auto data=QJsonDocument::fromJson(file.readAll(),&error).object();
    const double origin=data["forecast_origin_epoch"].toDouble(-1);
    const auto points=data["predictions"].toArray();
    if(error.error!=QJsonParseError::NoError || data["schema_version"].toInt()!=1 ||
       data["source"].toString()!="python-ml" || data["station_id_space"].toString()!="qt-database" ||
       data["model_version"].toString().isEmpty() || !data["simulated"].isBool() ||
       origin<0 || origin>QDateTime::currentSecsSinceEpoch()+60 ||
       std::fmod(origin,3600)!=0 || points.isEmpty()) return unavailable("invalid_result");
    QMap<int,QSet<int>> leads;
    for(const auto &value:points) {
        const auto point=value.toObject();
        const int station=point["station_id"].toInt(-1), lead=point["horizon_hours"].toInt(-1);
        const double load=point["predicted_load_kw"].toDouble(-1);
        const double available=point["predicted_available_piles"].toDouble(-1);
        const double occupied=point["predicted_occupied_piles"].toDouble(-1);
        const double congestion=point["congestion_ratio"].toDouble(-1);
        if(station<1 || lead<1 || lead>24 || leads[station].contains(lead) ||
           point["predicted_for_epoch"].toDouble(-1)!=origin+lead*3600 ||
           load<0 || available<0 || std::floor(available)!=available ||
           occupied<0 || std::floor(occupied)!=occupied || congestion<0 || congestion>1)
            return unavailable("invalid_result");
        leads[station].insert(lead);
    }
    for(const auto &hours:leads) if(hours.size()!=24) return unavailable("invalid_result");
    const auto age=QDateTime::currentSecsSinceEpoch()-static_cast<qint64>(origin);
    if(age < -60) return unavailable("invalid_result");
    return {{"available",true},{"stale",age>maxAgeSeconds},
            {"age_seconds",qMax<qint64>(0,age)},{"max_age_seconds",maxAgeSeconds},{"data",data}};
}
