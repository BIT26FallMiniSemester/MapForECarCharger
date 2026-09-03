#ifndef NAVIGATIONDIALOG_H
#define NAVIGATIONDIALOG_H

#include "models.h"

#include <QDialog>
#include <QUrl>

QT_BEGIN_NAMESPACE
namespace Ui {
class NavigationDialog;
}
QT_END_NAMESPACE

class QWebEngineView;
class TencentGeocoder;

class NavigationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NavigationDialog(QWidget *parent = nullptr);
    ~NavigationDialog();

    void setDestination(const StationSummary &station);
    void setOrigin(double latitude, double longitude, const QString &displayName);

private slots:
    void onSearchOrigin();
    void onNavigateClicked();
    void onGeocodeOk(double latitude, double longitude, const QString &name);
    void onGeocodeFail(const QString &message);

private:
    void updateOriginEnabled();
    void loadRoute(double fromLat, double fromLng, const QString &fromName);
    void setupMapView();
    void onMapLoadFinished(bool ok);

    Ui::NavigationDialog *ui;
    QWebEngineView *m_web = nullptr;
    TencentGeocoder *m_geo = nullptr;
    QUrl m_lastRouteUrl;
    bool m_waitingRoute = false;

    double m_currentLat = 0;
    double m_currentLng = 0;
    QString m_currentName;

    double m_customLat = 0;
    double m_customLng = 0;
    QString m_customName;
    bool m_hasCustom = false;
    bool m_pendingNavigate = false;

    double m_toLat = 0;
    double m_toLng = 0;
    QString m_toName;
};

#endif
