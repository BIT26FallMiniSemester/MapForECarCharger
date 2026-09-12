#pragma once

#include <QJsonObject>
#include <QObject>

class ForecastWorker : public QObject
{
    Q_OBJECT
public slots:
    void calculate(const QJsonObject &input);
signals:
    void ready(const QJsonObject &result);
};
