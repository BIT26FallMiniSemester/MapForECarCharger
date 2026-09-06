#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QList>
namespace Ui { class MainWindow; }
class ApiClient;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(bool demoMode = true, const QString &baseUrl = QString(), const QString &token = QString(), QWidget *parent = nullptr);
    ~MainWindow();
private:
    Ui::MainWindow *ui;
    QWidget *createDashboard();
    QWidget *createPilePage();
    QWidget *createStationPage();
    QWidget *createUserPage();
    QWidget *metricCard(const QString &title, const QString &value, const QString &hint);
    void buildTrendChart(int days);
    void buildStatusChart();
    void buildTrendChart(const QJsonArray &points);
    void buildStatusChart(const QJsonArray &items);
    void refreshPiles();
    void refreshStations();
    void refreshUsers();
    void showPileDetails();
    void restartSelectedPile();
    void showStationDetails();
    void editStation(bool create);
    void addPile();
    void showUserDetails();
    void toggleUserStatus();
    void loadDashboard(int days = 7);
    void handleApiSuccess(const QString &path, const QJsonValue &data, const QJsonObject &payload);
    void handleApiFailure(const QString &path, int httpStatus, int code, const QString &message);
    bool m_demoMode = true;
    ApiClient *m_api = nullptr;
    int m_trendDays = 7;
    int m_pilePage = 1, m_stationPage = 1, m_userPage = 1;
    int m_pilePages = 1, m_stationPages = 1, m_userPages = 1;
    QList<QLabel *> m_metricValues;
    class QLabel *m_pilePageLabel = nullptr;
    class QLabel *m_stationPageLabel = nullptr;
    class QLabel *m_userPageLabel = nullptr;
    QTableWidget *m_pileTable = nullptr;
    QTableWidget *m_stationTable = nullptr;
    QTableWidget *m_userTable = nullptr;
    QWidget *m_trendHost = nullptr;
    QWidget *m_statusHost = nullptr;
    class QComboBox *m_stationFilter = nullptr;
    class QComboBox *m_statusFilter = nullptr;
    class QLineEdit *m_pileSearch = nullptr;
    class QLineEdit *m_userSearch = nullptr;
};
#endif
