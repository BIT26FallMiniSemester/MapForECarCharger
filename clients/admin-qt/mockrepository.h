// 声明管理端演示模式使用的内存数据结构和操作接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef MOCKREPOSITORY_H
#define MOCKREPOSITORY_H

#include <QDate>
#include <QList>
#include <QString>

// 实现 Pile 的本地处理逻辑，保持与项目其他模块的接口约定一致。
struct Pile { int id; QString number, station, type, status, heartbeat; double power; int sessions, minutes; };
// 实现 Station 的本地处理逻辑，保持与项目其他模块的接口约定一致。
struct Station { int id; QString name, address, status; double latitude, longitude, price; int total, idle; double onlineRate; };
// 实现 User 的本地处理逻辑，保持与项目其他模块的接口约定一致。
struct User { int id; QString phone, nickname, status, registered; double balance, spent; int orders; bool hasActiveOrder; };
// 实现 TrendPoint 的本地处理逻辑，保持与项目其他模块的接口约定一致。
struct TrendPoint { QDate date; double revenue; int orders; };

// 初始化管理端演示数据仓库。
class MockRepository
{
public:
// 返回进程级唯一的演示数据仓库单例。
    static MockRepository &instance();
// 返回内存中的电桩列表。
    QList<Pile> &piles();
// 返回内存中的站点列表。
    QList<Station> &stations();
// 返回内存中的用户列表。
    QList<User> &users();
// 生成指定天数的演示营收趋势。
    QList<TrendPoint> trend(int days) const;
// 在演示模式中把故障电桩恢复为空闲并同步站点统计。
    bool restartPile(int id, QString &message);
// 向演示仓库追加站点。
    void addStation(const Station &station);
// 为指定站点创建电桩。
    void addPile(const Pile &pile);
private:
// 初始化管理端演示数据仓库。
    MockRepository();
    QList<Pile> m_piles;
    QList<Station> m_stations;
    QList<User> m_users;
};
#endif
