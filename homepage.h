#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include "models.h"

#include <QEvent>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class HomePage;
}
QT_END_NAMESPACE

class QComboBox;

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
    QString keyword() const;
    QString selectedDistrict() const;
    QString selectedOperator() const;
    QString selectedRegionScope() const;
    QString selectedLocationType() const;
    bool availableOnly() const;
    bool bookableOnly() const;

    void setBusy(bool busy);
    void showStations(const QVector<StationSummary> &stations);
    void showSearchStations(const QVector<StationSummary> &stations, int total);
    void setFilterOptions(const StationFilterOptions &options);
    void showHint(const QString &text);
    void setLocation(double lat, double lng, const QString &displayName);
    void useSimulatedGps();
    void resetToDistanceMode();
    void setActiveOrder(bool hasOrder, const ChargingOrder &order);
    bool isKeywordMode() const { return m_keywordMode; }

signals:
    void queryClicked();
    void searchClicked();
    void changeLocationClicked();
    void stationClicked(const StationSummary &station);
    void activeOrderClicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshLocationButton();
    void setupRadiusControl();
    void setupFilterCombos();
    void setupModeSwitch();
    void setKeywordMode(bool keywordMode);
    void setRadiusKm(int km);
    void fillFilterCombo(QComboBox *box, const QString &allLabel, const QStringList &values);
    void clearCards();
    void renderStations(const QVector<StationSummary> &stations, const QString &emptyHint, const QString &okHint);

    Ui::HomePage *ui;
    double m_lat = 39.9042;
    double m_lng = 116.4074;
    int m_radiusKm = 10;
    QString m_displayName = QStringLiteral("北京市东城区（模拟定位）");
    QVector<StationSummary> m_stations;
    bool m_keywordMode = false;
};

#endif
