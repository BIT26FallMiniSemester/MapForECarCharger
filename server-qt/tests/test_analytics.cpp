#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include "analyticsresult.h"

class AnalyticsTests: public QObject {
    Q_OBJECT
private slots:
    void forecastStates() {
        QTemporaryDir directory;
        const auto path=directory.filePath("forecast.json");
        const auto origin=(QDateTime::currentSecsSinceEpoch()/3600-1)*3600;
        QJsonArray points;
        for(int lead=1;lead<=24;++lead) points.append(QJsonObject{
            {"station_id",101},{"horizon_hours",lead},{"predicted_for_epoch",origin+lead*3600},
            {"predicted_load_kw",7.0},{"predicted_available_piles",3},
            {"predicted_occupied_piles",1},{"congestion_ratio",0.25}});
        QJsonObject data{{"schema_version",1},{"source","python-ml"},{"simulated",true},
            {"station_id_space","qt-database"},{"model_version","test"},
            {"forecast_origin_epoch",origin},{"predictions",points}};
        auto write=[&](){QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(data).toJson());};
        write();
        QVERIFY(readForecastResult(path)["available"].toBool());
        QVERIFY(!readForecastResult(path)["stale"].toBool());
        QVERIFY(readForecastResult(path,1)["stale"].toBool());
        auto invalid=points;invalid.append(points.first());data["predictions"]=invalid;write();
        QCOMPARE(readForecastResult(path)["reason"].toString(),QString("invalid_result"));
        invalid=points;invalid.removeLast();data["predictions"]=invalid;write();
        QVERIFY(!readForecastResult(path)["available"].toBool());
        invalid=points;auto point=invalid[0].toObject();point["predicted_available_piles"]=1.5;
        invalid[0]=point;data["predictions"]=invalid;write();
        QVERIFY(!readForecastResult(path)["available"].toBool());
        data["predictions"]=points;data["forecast_origin_epoch"]=1e100;write();
        QVERIFY(!readForecastResult(path)["available"].toBool());
    }
    void resultStates() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path=directory.filePath("analytics.json");
        QCOMPARE(readAnalyticsResult("")["reason"].toString(),QString("not_configured"));
        QCOMPARE(readAnalyticsResult(path)["reason"].toString(),QString("missing_result"));
        auto write=[&](const QByteArray &bytes) {QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(bytes);};
        write("invalid");
        QCOMPARE(readAnalyticsResult(path)["reason"].toString(),QString("invalid_result"));
        QJsonArray days;
        for(int i=0;i<30;++i) days.append(QJsonObject{{"date","2026-09-12"}});
        QJsonObject data{{"schema_version",1},{"engine","hadoop-mapreduce"},{"run_id","test"},
            {"snapshot_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"generated_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"total_revenue_cents",0},{"revenue_trend",QJsonObject{{"items",days}}},
            {"station_ranking",QJsonObject{{"items",QJsonArray{}}}}};
        write(QJsonDocument(data).toJson());
        QVERIFY(readAnalyticsResult(path)["available"].toBool());
        QVERIFY(!readAnalyticsResult(path)["stale"].toBool());
        data["snapshot_at"]=QDateTime::currentDateTimeUtc().addSecs(-1000).toString(Qt::ISODateWithMs);
        write(QJsonDocument(data).toJson());
        QVERIFY(readAnalyticsResult(path)["stale"].toBool());
        data["schema_version"]=2;
        write(QJsonDocument(data).toJson());
        QVERIFY(!readAnalyticsResult(path)["available"].toBool());
    }
};
QTEST_GUILESS_MAIN(AnalyticsTests)
#include "test_analytics.moc"
