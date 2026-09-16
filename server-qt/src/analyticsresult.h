#pragma once
#include <QJsonObject>
#include <QString>

// Read only a configured published file, never a path supplied by an HTTP client.
QJsonObject readAnalyticsResult(const QString &path, int maxAgeSeconds = 900);
QJsonObject readForecastResult(const QString &path, int maxAgeSeconds = 7200);
QJsonObject readSparkAnalytics(const QString &root, const QString &batch, int maxAgeSeconds = 86400);
