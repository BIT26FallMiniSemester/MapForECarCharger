#include "common.h"
#include <QPasswordDigestor>
#include <QtEndian>
#include <cmath>
#include <limits>

void fail(int code) {
    static const QMap<int, QString> messages{
        {40001, QStringLiteral("请求内容不正确")}, {40002, QStringLiteral("您已有未完成订单")},
        {40003, QStringLiteral("电桩不可预约或已被占用")}, {40004, QStringLiteral("当前状态不允许该操作")},
        {40005, QStringLiteral("预约已过期")}, {40006, QStringLiteral("余额不足")},
        {40007, QStringLiteral("用户有未完成订单，不能冻结")}, {40008, QStringLiteral("电桩编号重复")},
        {40009, QStringLiteral("未知操作")}, {40010, QStringLiteral("不支持的协议版本")},
        {40101, QStringLiteral("请重新登录")}, {40301, QStringLiteral("没有权限或账号已停用")},
        {40401, QStringLiteral("数据不存在")}, {50000, QStringLiteral("内部处理失败")},
        {50001, QStringLiteral("数据库暂时不可用")}, {50301, QStringLiteral("腾讯地图服务暂时不可用")}
    };
    throw Failure{code, messages.value(code, QStringLiteral("处理失败"))};
}
QString utcNow() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString randomToken() {
    QByteArray bytes(32, Qt::Uninitialized);
    for (int i=0; i<32; i+=4) { quint32 n=QRandomGenerator::system()->generate(); memcpy(bytes.data()+i,&n,4); }
    return QString::fromLatin1(bytes.toHex());
}
QString passwordHash(const QString &password) {
    const auto salt=QByteArray::fromHex(randomToken().left(32).toLatin1());
    const auto hash=QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,password.toUtf8(),salt,310000,32);
    return "pbkdf2_sha256$310000$"+QString::fromLatin1(salt.toHex())+"$"+QString::fromLatin1(hash.toHex());
}
bool passwordVerify(const QString &password,const QString &encoded) {
    const auto parts=encoded.split('$');
    if(parts.size()!=4 || parts[0]!="pbkdf2_sha256") return false;
    bool ok=false; int rounds=parts[1].toInt(&ok);
    static const QRegularExpression hex("^[0-9a-fA-F]+$");
    if(!ok || rounds<10000 || rounds>1000000 || parts[2].size()!=32 || parts[3].size()!=64
       || !hex.match(parts[2]).hasMatch() || !hex.match(parts[3]).hasMatch()) return false;
    const auto actual=QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,password.toUtf8(),QByteArray::fromHex(parts[2].toLatin1()),rounds,32);
    const auto expected=QByteArray::fromHex(parts[3].toLatin1());
    unsigned char difference=0;
    for(int i=0;i<32;i++) difference |= static_cast<unsigned char>(actual[i]^expected[i]);
    return difference==0;
}
qint64 integer(const QJsonValue &v) {
    if(!v.isDouble() || !std::isfinite(v.toDouble()) || std::floor(v.toDouble())!=v.toDouble()
       || std::abs(v.toDouble())>9007199254740991.0) fail(40001);
    return v.toInteger();
}
qint64 roundedProduct(qint64 a,qint64 b,qint64 divisor) {
    if(a<0 || b<0 || divisor<=0 || (b && a>std::numeric_limits<qint64>::max()/b)) fail(40001);
    const qint64 n=a*b, result=n/divisor+(n%divisor >= (divisor+1)/2);
    if(result>9007199254740991LL) fail(40001);
    return result;
}
QJsonObject contract() {
    static const QJsonObject spec=[] {
        QFile file(":/schemas/socket.json");
        if(!file.open(QIODevice::ReadOnly)) qFatal("Cannot load embedded Socket contract");
        const auto doc=QJsonDocument::fromJson(file.readAll());
        if(!doc.isObject()) qFatal("Invalid embedded Socket contract");
        return doc.object();
    }();
    return spec;
}
bool validate(const QJsonValue &v,QJsonObject s) {
    if(s.contains("$ref")) s=contract()["$defs"].toObject()[s["$ref"].toString().section('/',-1)].toObject();
    if(s.contains("anyOf")) { for(auto option:s["anyOf"].toArray()) if(validate(v,option.toObject())) return true; return false; }
    const QString type=s["type"].toString();
    if(type=="null") return v.isNull();
    if(type=="object") {
        if(!v.isObject()) return false;
        const auto o=v.toObject(), properties=s["properties"].toObject();
        for(auto field:s["required"].toArray()) if(!o.contains(field.toString())) return false;
        for(auto it=o.begin();it!=o.end();++it) {
            if(!properties.contains(it.key())) { if(s["additionalProperties"].isBool()&&!s["additionalProperties"].toBool()) return false; }
            else if(!validate(it.value(),properties[it.key()].toObject())) return false;
        }
    } else if(type=="array") {
        if(!v.isArray()) return false;
        for(auto item:v.toArray()) if(!validate(item,s["items"].toObject())) return false;
    } else if(type=="string") {
        if(!v.isString()) return false;
        const auto text=v.toString();
        if(text.size()<s["minLength"].toInt(0) || text.size()>s["maxLength"].toInt(INT_MAX)) return false;
        if(s.contains("pattern")&&!QRegularExpression(s["pattern"].toString()).match(text).hasMatch()) return false;
    } else if(type=="integer" || type=="number") {
        if(!v.isDouble() || !std::isfinite(v.toDouble())) return false;
        if(type=="integer") { try{integer(v);}catch(const Failure&){return false;} }
        if(s.contains("minimum")&&v.toDouble()<s["minimum"].toDouble()) return false;
        if(s.contains("maximum")&&v.toDouble()>s["maximum"].toDouble()) return false;
    } else if(type=="boolean" && !v.isBool()) return false;
    if(s.contains("enum")&&!s["enum"].toArray().contains(v)) return false;
    if(s.contains("const")&&v!=s["const"]) return false;
    return true;
}
QByteArray frame(const QJsonObject &o) {
    QByteArray body=QJsonDocument(o).toJson(QJsonDocument::Compact), out(4,Qt::Uninitialized);
    qToBigEndian<quint32>(body.size(),out.data()); return out+body;
}
