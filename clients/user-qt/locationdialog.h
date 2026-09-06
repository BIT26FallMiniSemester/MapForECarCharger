#ifndef LOCATIONDIALOG_H
#define LOCATIONDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class LocationDialog;
}
QT_END_NAMESPACE

class TencentGeocoder;

class LocationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LocationDialog(QWidget *parent = nullptr);
    ~LocationDialog();

    double latitude() const { return m_lat; }
    double longitude() const { return m_lng; }
    QString displayName() const { return m_displayName; }

    void setCurrentLocation(double lat, double lng, const QString &displayName);

private slots:
    void onUseRegion();
    void onSearchAddress();
    void onLocateMyself();
    void onGeocodeOk(double lat, double lng, const QString &name);
    void onGeocodeFail(const QString &message);
    void onAccepted();

private:
    void applyLocation(double lat, double lng, const QString &name);
    void fillRegions();

    Ui::LocationDialog *ui;
    TencentGeocoder *m_geo = nullptr;
    double m_lat = 39.9042;
    double m_lng = 116.4074;
    QString m_displayName = QStringLiteral("北京市东城区（模拟定位）");
};

#endif
