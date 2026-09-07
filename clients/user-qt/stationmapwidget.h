#ifndef STATIONMAPWIDGET_H
#define STATIONMAPWIDGET_H

#include "models.h"

#include <QPixmap>
#include <QVector>
#include <QWidget>

class QLabel;
class QToolButton;

class StationMapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StationMapWidget(QWidget *parent = nullptr);
    void setCenter(double latitude, double longitude, int zoom = 14);
    void setImage(const QByteArray &png);
    void setStations(const QVector<StationSummary> &stations);
    void selectStation(qint64 stationId);
signals:
    void stationFocused(const StationSummary &station);
    void zoomChanged(double centerLatitude, double centerLongitude, int zoom);
private:
    void rebuildPins();
    QPoint pointFor(double latitude, double longitude) const;
    void changeZoom(int delta);
    QLabel *m_background = nullptr;
    QToolButton *m_zoomIn = nullptr;
    QToolButton *m_zoomOut = nullptr;
    QPixmap m_image;
    QVector<StationSummary> m_stations;
    double m_userLatitude = 39.9042;
    double m_userLongitude = 116.4074;
    double m_centerLatitude = 39.9042;
    double m_centerLongitude = 116.4074;
    int m_zoom = 14;
    qint64 m_selectedStationId = 0;
};

#endif
