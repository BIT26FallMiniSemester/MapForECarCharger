#ifndef CHARGINGPAGE_H
#define CHARGINGPAGE_H

#include "models.h"

#include <QWidget>

class ApiClient;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

class ChargingPage : public QWidget
{
    Q_OBJECT
public:
    explicit ChargingPage(ApiClient *api, QWidget *parent = nullptr);
    void selectStation(const StationSummary &station, double fromLatitude, double fromLongitude);
    void restoreActiveOrder();
    void clearSession();
    void setBusy(bool busy);

private:
    void clearPiles();
    void renderOrder();
    void setHint(const QString &text, bool error = false);
    bool hasActiveOrder() const;
    QString statusText(const QString &status) const;

    ApiClient *m_api;
    StationSummary m_station;
    ChargingOrder m_order;
    double m_fromLatitude = 0;
    double m_fromLongitude = 0;
    QLabel *m_stationName = nullptr;
    QLabel *m_stationInfo = nullptr;
    QLabel *m_routeInfo = nullptr;
    QLabel *m_orderTitle = nullptr;
    QLabel *m_orderStatus = nullptr;
    QLabel *m_metrics = nullptr;
    QLabel *m_hint = nullptr;
    QWidget *m_pileHost = nullptr;
    QVBoxLayout *m_pileLayout = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_settle = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_refresh = nullptr;
    QTimer *m_timer = nullptr;
};

#endif
