#ifndef STATIONMAPWIDGET_H
#define STATIONMAPWIDGET_H

#include "models.h"

#include <QPixmap>
#include <QVector>
#include <QWidget>

class QLabel;

class StationMapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StationMapWidget(QWidget *parent = nullptr);
    void setCenter(double latitude, double longitude, int zoom = 12);
    void setImage(const QByteArray &png);
    void setStations(const QVector<StationSummary> &stations);
signals:
    void stationFocused(const StationSummary &station);
private:
    void rebuildPins();
    QPoint pointFor(double latitude, double longitude) const;
    QLabel *m_background = nullptr;
    QPixmap m_image;
    QVector<StationSummary> m_stations;
    double m_latitude = 39.9042;
    double m_longitude = 116.4074;
    int m_zoom = 12;
};

#endif
