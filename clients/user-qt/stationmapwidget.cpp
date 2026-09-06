#include "stationmapwidget.h"

#include <QLabel>
#include <QToolButton>
#include <QtMath>

namespace {
QPointF worldPoint(double latitude,double longitude,int zoom)
{
    const double scale=256.0*qPow(2.0,zoom);const double sinLat=qSin(qDegreesToRadians(qBound(-85.05112878,latitude,85.05112878)));
    return {scale*(longitude+180.0)/360.0,scale*(0.5-qLn((1.0+sinLat)/(1.0-sinLat))/(4.0*M_PI))};
}
}

StationMapWidget::StationMapWidget(QWidget *parent):QWidget(parent),m_background(new QLabel(this))
{
    setFixedSize(600,300);setObjectName(QStringLiteral("stationMap"));m_background->setGeometry(rect());m_background->setAlignment(Qt::AlignCenter);m_background->setText(QStringLiteral("正在加载腾讯地图…"));m_background->setStyleSheet(QStringLiteral("background:#dcece8;color:#38635b;border-radius:18px;"));
}

void StationMapWidget::setCenter(double latitude,double longitude,int zoom){m_latitude=latitude;m_longitude=longitude;m_zoom=zoom;rebuildPins();}
void StationMapWidget::setImage(const QByteArray &png){m_image.loadFromData(png,"PNG");m_background->setPixmap(m_image);rebuildPins();}
void StationMapWidget::setStations(const QVector<StationSummary> &stations){m_stations=stations;rebuildPins();}

QPoint StationMapWidget::pointFor(double latitude,double longitude) const
{
    const QPointF center=worldPoint(m_latitude,m_longitude,m_zoom),point=worldPoint(latitude,longitude,m_zoom);
    return QPoint(qRound(width()/2.0+point.x()-center.x()),qRound(height()/2.0+point.y()-center.y()));
}

void StationMapWidget::rebuildPins()
{
    const auto old=findChildren<QToolButton*>(QString(),Qt::FindDirectChildrenOnly);for(auto *button:old)button->deleteLater();
    auto *me=new QToolButton(this);me->setText(QStringLiteral("●"));me->setToolTip(QStringLiteral("我的位置"));me->setGeometry(width()/2-13,height()/2-13,26,26);me->setStyleSheet(QStringLiteral("QToolButton{background:#f59e0b;color:white;border:3px solid white;border-radius:13px;font-size:13px;}"));me->show();
    for(int i=0;i<qMin(10,m_stations.size());++i){const auto station=m_stations[i];const QPoint point=pointFor(station.latitude,station.longitude);if(!rect().adjusted(16,16,-16,-16).contains(point))continue;auto *pin=new QToolButton(this);pin->setText(QString::number(i+1));pin->setToolTip(station.name);pin->setGeometry(point.x()-15,point.y()-30,30,30);pin->setCursor(Qt::PointingHandCursor);pin->setStyleSheet(QStringLiteral("QToolButton{background:#0f766e;color:white;border:3px solid white;border-radius:15px;font-weight:700;}QToolButton:hover{background:#e11d48;}"));connect(pin,&QToolButton::clicked,this,[this,station]{emit stationFocused(station);});pin->show();}
}
