#ifndef LOCATIONDIALOG_H
#define LOCATIONDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class LocationDialog;
}
QT_END_NAMESPACE

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

public slots:
    void applyGeocodedLocation(double lat, double lng, const QString &displayName);
    void showSearchError(const QString &message);

signals:
    void searchRequested(const QString &address);

private slots:
    void onUseRegion();
    void onSearch();
    void onAccepted();

private:
    void applyLocation(double lat, double lng, const QString &name);
    void fillRegions();

    Ui::LocationDialog *ui;
    double m_lat = 39.9042;
    double m_lng = 116.4074;
    QString m_displayName = QStringLiteral("北京市中心");
    bool m_searching = false;
};

#endif
