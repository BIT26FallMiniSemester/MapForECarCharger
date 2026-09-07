#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include "models.h"

#include <QByteArray>
#include <QHash>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class HomePage;
}
QT_END_NAMESPACE

class QEvent;
class QFrame;
class QLabel;
class QPushButton;

class HomePage : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);
    ~HomePage();

    double latitude() const { return m_lat; }
    double longitude() const { return m_lng; }
    double radiusKm() const;
    QString displayName() const { return m_displayName; }

    void setBusy(bool busy);
    void showStations(const QVector<StationSummary> &stations);
    void showHint(const QString &text);
    void setLocation(double lat, double lng, const QString &displayName);
    void showMap(const QByteArray &png);
    void showMapError(const QString &message);
    void useSimulatedGps();

signals:
    void queryClicked();
    void changeLocationClicked();
    void stationSelected(const StationSummary &station);
    void mapZoomRequested(double centerLatitude, double centerLongitude, int zoom);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void refreshLocationButton();
    void setupRadiusControl();
    void setRadiusKm(int km);
    void clearCards();
    void focusStation(const StationSummary &station);

    Ui::HomePage *ui;
    double m_lat = 39.9042;
    double m_lng = 116.4074;
    int m_radiusKm = 10;
    QString m_displayName = QStringLiteral("北京市中心（可修改）");
    class StationMapWidget *m_map = nullptr;
    QLabel *m_mapDetail = nullptr;
    QPushButton *m_mapCharge = nullptr;
    QVector<StationSummary> m_stations;
    QHash<qint64,QFrame*> m_cards;
    StationSummary m_focusedStation;
};

#endif
