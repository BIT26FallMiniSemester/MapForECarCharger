#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include "analyticsresult.h"

class AnalyticsTests: public QObject {
    Q_OBJECT
private slots:
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
