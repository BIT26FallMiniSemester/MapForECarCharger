#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "models.h"

#include <QMainWindow>
#include <QPointer>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class ApiClient;
class OrderDialog;
class StationDetailDialog;
class TencentGeocoder;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onLoginClicked();
    void onLoginSucceeded(const QString &token, const User &user, bool isNewUser);
    void onSelfLocated(double lat, double lng, const QString &name);
    void onSelfLocateFailed(const QString &message);
    void onQueryNearby();
    void onSearchStations();
    void onChangeLocation();
    void onStationClicked(const StationSummary &station);
    void onEditProfile();
    void onRecharge();
    void onRechargeRecords();
    void onOrderRecords();
    void onActiveOrderClicked();
    void onLogout();
    void onApiFailed(int code, const QString &message);
    void setBusy(bool busy);

private:
    void applyTheme();
    void showAppPage();
    void applyUser(const User &user);
    void applyActiveOrder(bool hasOrder, const ChargingOrder &order);
    void openOrderDialog(const ChargingOrder &order, QWidget *parent = nullptr);
    void warnUnfinishedOrder(QWidget *parent = nullptr);
    void refreshStationPiles();
    QString avatarSettingKey() const;

    Ui::MainWindow *ui;
    ApiClient *m_api = nullptr;
    TencentGeocoder *m_geo = nullptr;
    User m_user;
    ChargingOrder m_activeOrder;
    ChargingOrder m_createdOrder;
    bool m_hasActiveOrder = false;
    bool m_autoReserve = false;
    bool m_openActiveWhenReady = false;
    QString m_pendingNickname;
    int m_inflight = 0;
    QPointer<StationDetailDialog> m_stationDialog;
    QPointer<OrderDialog> m_orderDialog;
};

#endif
