// 声明服务端业务层的会话、用户、订单、充电桩、站点和运营管理操作。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#pragma once
#include "database.h"

// 保存已认证用户或管理员的编号、角色和会话过期时间。
struct Identity { qint64 id=0; QString role; QDateTime expires; };
// 持有数据库引用和内存会话集合，是服务端业务处理入口。
class Business {
public:
    explicit Business(Database &database): db(database) {}
// 按 action 声明的角色校验令牌、账户状态和会话有效期。
    Identity authorize(const QString &action,const QString &token);
// 根据 action 将请求分派到用户、钱包、订单、地图或管理员操作。
    QJsonValue dispatch(const QString &action,const QJsonObject &data,const Identity &identity);
// 取消过期预约、释放电桩并清理已失效的内存会话。
    void expire();
// 查询站点基础信息并附加电桩总数、在线数和空闲数。
    QJsonObject station(qint64 id);
// 读取启用站点作为地图距离矩阵的候选集合。
    QJsonArray nearbyCandidates();
private:
    Database &db;
    QHash<QString,Identity> sessions;
// 查询用户资料及订单数、累计消费等汇总字段。
    QJsonObject user(qint64 id);
// 查询电桩资料及累计充电次数和累计时长。
    QJsonObject pile(qint64 id);
// 查询订单资料，补充站点/电桩信息，并估算进行中订单的电量与金额。
    QJsonObject order(qint64 id);
// 实现用户侧订单列表、创建、预约、开始、结束、支付和取消状态流转。
    QJsonValue orderAction(const QString &action,const QJsonObject &data,qint64 userId);
// 实现管理员统计、目录维护、用户状态、电桩恢复和操作日志。
    QJsonValue adminAction(const QString &action,const QJsonObject &data,qint64 adminId);
// 把电桩状态变化写入 pile_status_logs 供追溯。
    void pileLog(qint64 pileId,qint64 orderId,const QString &before,const QString &after,const QString &reason);
// 把管理员操作及详情写入 operation_logs 供审计。
    void operationLog(qint64 admin,const QString &action,const QString &type,qint64 target,const QJsonObject &data);
    QJsonObject listing(const QString &sql,const QVariantList &args,const QJsonObject &data,const std::function<QJsonObject(QJsonObject)> &transform={});
};
