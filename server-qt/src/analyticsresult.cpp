#include "analyticsresult.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <cmath>
#include <QDir>
#include <QVector>
#include <algorithm>
QJsonObject readSparkAnalytics(const QString &root, const QString &batch, int maxAgeSeconds)
{
    auto unavailable=[](){return QJsonObject{{"available",false},{"reason","ads_not_ready"}};};
    if(root.isEmpty() || batch.isEmpty() || batch.contains('/') || batch.contains('\\') || batch=="..") return unavailable();
    bool valid=true;
    auto load=[&](const QString &table) {
        QJsonArray rows;
        QDir dir(root+"/"+table+"/batch_id="+batch+"/json");
        if(!dir.exists("_SUCCESS")) {valid=false;return rows;}
        const auto parts=dir.entryList({"part-*.json"},QDir::Files,QDir::Name);
        if(parts.isEmpty()) valid=false;
        qint64 size=0;
        for(const auto &part:parts) {
            QFile file(dir.filePath(part));
            if(!file.open(QIODevice::ReadOnly) || (size+=file.size())>2*1024*1024) {valid=false;break;}
            while(!file.atEnd()) {
                const auto line=file.readLine().trimmed();if(line.isEmpty())continue;
                QJsonParseError error;const auto doc=QJsonDocument::fromJson(line,&error);
                if(error.error!=QJsonParseError::NoError || !doc.isObject()) {valid=false;break;}
                rows.append(doc.object());
            }
        }
        return rows;
    };
    const auto overview=load("ads_overview");auto trend=load("ads_revenue_trend_30d");auto ranking=load("ads_station_ranking_30d");
    if(!valid || overview.size()!=1 || trend.isEmpty())return unavailable();
    QVector<QJsonObject> sorted;
    for(const auto &value:trend) {auto row=value.toObject();row["date"]=row["biz_date"];sorted.append(row);}
    std::sort(sorted.begin(),sorted.end(),[](const auto &a,const auto &b){return a["date"].toString()<b["date"].toString();});
    trend=QJsonArray();for(const auto &row:sorted)trend.append(row);
    QVector<QJsonObject> ranked;for(const auto &value:ranking)ranked.append(value.toObject());
    std::sort(ranked.begin(),ranked.end(),[](const auto &a,const auto &b){const auto ar=a["revenue_cents"].toDouble(),br=b["revenue_cents"].toDouble();return ar==br?a["station_id"].toInt()<b["station_id"].toInt():ar>br;});
    ranking=QJsonArray();for(int i=0;i<qMin(10,int(ranked.size()));++i)ranking.append(ranked[i]);
    const auto row=overview.first().toObject();
    const auto snapshot=QDateTime::fromString(row["generated_at"].toString(),Qt::ISODateWithMs);
    if(!row["total_revenue_cents"].isDouble() || !snapshot.isValid() || snapshot.secsTo(QDateTime::currentDateTimeUtc()) < -60)return unavailable();
    const auto age=qMax<qint64>(0,snapshot.secsTo(QDateTime::currentDateTimeUtc()));
    return {{"available",true},{"stale",age>maxAgeSeconds},{"age_seconds",age},{"data",QJsonObject{
        {"schema_version",1},{"engine","spark-sql"},{"batch_id",batch},{"snapshot_at",row["generated_at"]},
        {"data_as_of",row["data_as_of"]},{"total_revenue_cents",row["total_revenue_cents"]},
        {"revenue_trend",QJsonObject{{"days",trend.size()},{"items",trend}}},
        {"station_ranking",QJsonObject{{"days",30},{"items",ranking}}}}}};
}


QJsonObject readAnalyticsResult(const QString &path, int maxAgeSeconds)
{
    if(!qEnvironmentVariable("ADS_ROOT").isEmpty()) return readSparkAnalytics(qEnvironmentVariable("ADS_ROOT"),qEnvironmentVariable("ADS_BATCH_ID"),maxAgeSeconds);
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
