// 提供不依赖服务端的站点、电桩、用户、趋势和电桩恢复演示数据。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "mockrepository.h"

/// 返回进程级唯一的演示数据仓库单例。
MockRepository &MockRepository::instance() { static MockRepository repo; return repo; }
/// 返回内存中的电桩列表。
QList<Pile> &MockRepository::piles() { return m_piles; }
/// 返回内存中的站点列表。
QList<Station> &MockRepository::stations() { return m_stations; }
/// 返回内存中的用户列表。
QList<User> &MockRepository::users() { return m_users; }

/// 初始化管理端演示数据仓库。
MockRepository::MockRepository()
{
    m_stations = {
        {1,"深圳市民中心充电站","福田区福中三路","ACTIVE",22.5431,114.0579,1.28,8,3,87.5},
        {2,"科技园超级充电站","南山区科技南路","ACTIVE",22.5344,113.9547,1.18,12,5,91.7},
        {3,"宝安中心充电站","宝安区创业一路","ACTIVE",22.5537,113.8831,1.35,6,1,83.3}
    };
    m_piles = {
        {1,"SZ-FUT-001","深圳市民中心充电站","快充","IDLE","刚刚",120,386,24120},
        {2,"SZ-FUT-002","深圳市民中心充电站","快充","CHARGING","8秒前",120,421,27600},
        {3,"SZ-FUT-003","深圳市民中心充电站","慢充","FAULT","2分钟前",7,193,14280},
        {4,"SZ-NS-001","科技园超级充电站","快充","RESERVED","刚刚",180,512,30120},
        {5,"SZ-NS-002","科技园超级充电站","快充","IDLE","5秒前",180,633,39300},
        {6,"SZ-NS-003","科技园超级充电站","快充","OFFLINE","3小时前",120,288,19800},
        {7,"SZ-BA-001","宝安中心充电站","慢充","IDLE","12秒前",7,156,18720},
        {8,"SZ-BA-002","宝安中心充电站","快充","CHARGING","刚刚",120,342,22440}
    };
    m_users = {
        {1001,"138****1201","粤B车主","NORMAL","2026-05-12 09:30",236.50,1820.40,26,false},
        {1002,"136****8842","小鹏车友","NORMAL","2026-06-03 14:18",98.20,960.80,15,true},
        {1003,"159****3176","绿色出行","FROZEN","2026-06-21 11:05",18.00,426.50,8,false},
        {1004,"188****5029","南山用户","NORMAL","2026-07-08 19:42",516.80,2145.60,31,false},
        {1005,"131****6633","充电达人","NORMAL","2026-08-01 08:16",72.30,775.20,12,false}
    };
}

/// 生成指定天数的演示营收趋势。
QList<TrendPoint> MockRepository::trend(int days) const
{
    QList<TrendPoint> result;
    for (int i = days - 1; i >= 0; --i) {
        double amount = 2500 + ((i * 379 + 913) % 2600);
        if (i == 4 || i == 17) amount = 0; // 展示“缺失日期补零”口径
        result.append({QDate::currentDate().addDays(-i), amount, int(amount / 42)});
    }
    return result;
}

/// 在演示模式中把故障电桩恢复为空闲并同步站点统计。
bool MockRepository::restartPile(int id, QString &message)
{
    for (Pile &p : m_piles) {
        if (p.id != id) continue;
        if (p.status == "IDLE") { message = "电桩已经处于空闲状态，无需重复重启"; return true; }
        if (p.status != "FAULT") { message = "仅故障（FAULT）电桩允许远程重启"; return false; }
        p.status = "IDLE"; p.heartbeat = "刚刚";
        for (Station &station : m_stations) if (station.name == p.station) { ++station.idle; break; }
        message = "远程重启成功，电桩已恢复空闲"; return true;
    }
    message = "未找到电桩"; return false;
}
/// 向演示仓库追加站点。
void MockRepository::addStation(const Station &s) { m_stations.append(s); }
/// 为指定站点创建电桩。
void MockRepository::addPile(const Pile &p) { m_piles.append(p); }
