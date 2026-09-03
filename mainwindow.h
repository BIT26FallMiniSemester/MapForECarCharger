#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "models.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class ApiClient;
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
    void onChangeLocation();
    void onStationClicked(const StationSummary &station);
    void onEditProfile();
    void onRecharge();
    void onRechargeRecords();
    void onOrderRecords();
    void onLogout();
    void onApiFailed(int code, const QString &message);
    void setBusy(bool busy);

private:
    void applyTheme();
    void showAppPage();
    void applyUser(const User &user);
    QString avatarSettingKey() const;

    Ui::MainWindow *ui;
    ApiClient *m_api = nullptr;
    TencentGeocoder *m_geo = nullptr;
    User m_user;
    int m_inflight = 0;
};

#endif
