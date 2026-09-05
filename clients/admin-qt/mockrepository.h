#ifndef MOCKREPOSITORY_H
#define MOCKREPOSITORY_H

#include <QDate>
#include <QList>
#include <QString>

struct Pile { int id; QString number, station, type, status, heartbeat; double power; int sessions, minutes; };
struct Station { int id; QString name, address, status; double latitude, longitude, price; int total, idle; double onlineRate; };
struct User { int id; QString phone, nickname, status, registered; double balance, spent; int orders; bool hasActiveOrder; };
struct TrendPoint { QDate date; double revenue; int orders; };

class MockRepository
{
public:
    static MockRepository &instance();
    QList<Pile> &piles();
    QList<Station> &stations();
    QList<User> &users();
    QList<TrendPoint> trend(int days) const;
    bool restartPile(int id, QString &message);
    void addStation(const Station &station);
    void addPile(const Pile &pile);
private:
    MockRepository();
    QList<Pile> m_piles;
    QList<Station> m_stations;
    QList<User> m_users;
};
#endif
