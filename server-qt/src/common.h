// 公共数据类型、错误码、工具函数和 Socket 协议基础能力。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#pragma once
#include <QtCore>
#include <functional>

// 实现 Failure 的本地处理逻辑，保持与项目其他模块的接口约定一致。
struct Failure { int code; QString message; };
// 根据业务码抛出包含固定中文提示的统一失败异常。
[[noreturn]] void fail(int code);
// 生成带毫秒的 UTC ISO-8601 时间字符串。
QString utcNow();
// 生成用于会话或文件名的随机十六进制令牌。
QString randomToken();
// 使用随机盐和 PBKDF2-SHA256 生成可持久化的密码摘要。
QString passwordHash(const QString &password);
// 解析并校验 PBKDF2 密码摘要，使用常量时间比较避免泄露差异。
bool passwordVerify(const QString &password, const QString &encoded);
// 把 JSON 数字安全转换为协议允许范围内的有符号整数。
qint64 integer(const QJsonValue &value);
// 计算整数乘除结果并按协议规则四舍五入，同时检查溢出。
qint64 roundedProduct(qint64 a, qint64 b, qint64 divisor);
// 从 Qt 资源加载并返回 Socket 协议的 JSON Schema 契约。
QJsonObject contract();
// 按 JSON Schema 对协议值执行类型、长度、范围、枚举和引用校验。
bool validate(const QJsonValue &value, QJsonObject schema);
// 把 JSON 对象编码为带 4 字节大端长度的协议帧。
QByteArray frame(const QJsonObject &message);
using Result = std::function<void(QJsonValue, int)>;
