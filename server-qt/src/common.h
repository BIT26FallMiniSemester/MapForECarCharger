#pragma once
#include <QtCore>
#include <functional>

struct Failure { int code; QString message; };
[[noreturn]] void fail(int code);
QString utcNow();
QString randomToken();
QString passwordHash(const QString &password);
bool passwordVerify(const QString &password, const QString &encoded);
qint64 integer(const QJsonValue &value);
qint64 roundedProduct(qint64 a, qint64 b, qint64 divisor);
QJsonObject contract();
bool validate(const QJsonValue &value, QJsonObject schema);
QByteArray frame(const QJsonObject &message);
using Result = std::function<void(QJsonValue, int)>;
