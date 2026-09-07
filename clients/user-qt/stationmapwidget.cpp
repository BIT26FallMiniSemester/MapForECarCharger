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
    setFixedSize(340,170);setObjectName(QStringLiteral("stationMap"));m_background->setGeometry(rect());m_background->setAlignment(Qt::AlignCenter);m_background->setText(QStringLiteral("正在加载腾讯地图…"));m_background->setStyleSheet(QStringLiteral("background:#dcece8;color:#38635b;border-radius:16px;"));
    m_zoomIn=new QToolButton(this);m_zoomOut=new QToolButton(this);
    for(auto *button:{m_zoomIn,m_zoomOut}){button->setObjectName(QStringLiteral("mapZoomButton"));button->setFixedSize(34,34);button->setCursor(Qt::PointingHandCursor);button->raise();}
    m_zoomIn->setText(QStringLiteral("+"));m_zoomOut->setText(QStringLiteral("−"));m_zoomIn->move(width()-46,12);m_zoomOut->move(width()-46,50);
    m_zoomIn->setToolTip(QStringLiteral("放大地图"));m_zoomOut->setToolTip(QStringLiteral("缩小地图"));
    connect(m_zoomIn,&QToolButton::clicked,this,[this]{changeZoom(1);});connect(m_zoomOut,&QToolButton::clicked,this,[this]{changeZoom(-1);});
}

void StationMapWidget::setCenter(double latitude,double longitude,int zoom){m_userLatitude=latitude;m_userLongitude=longitude;m_centerLatitude=latitude;m_centerLongitude=longitude;m_zoom=zoom;rebuildPins();}
void StationMapWidget::setImage(const QByteArray &png){m_image.loadFromData(png,"PNG");m_background->setPixmap(m_image.scaled(size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation));rebuildPins();}
void StationMapWidget::setStations(const QVector<StationSummary> &stations){m_stations=stations;if(!stations.isEmpty()&&m_selectedStationId==0)m_selectedStationId=stations.first().id;rebuildPins();}
void StationMapWidget::selectStation(qint64 stationId){m_selectedStationId=stationId;rebuildPins();}

void StationMapWidget::changeZoom(int delta)
{
    const int next=qBound(11,m_zoom+delta,17);if(next==m_zoom)return;m_zoom=next;
    for(const auto &station:m_stations)if(station.id==m_selectedStationId){m_centerLatitude=station.latitude;m_centerLongitude=station.longitude;break;}
    m_image=QPixmap();m_background->clear();m_background->setText(QStringLiteral("正在加载 %1 级地图…").arg(m_zoom));rebuildPins();m_zoomIn->setEnabled(m_zoom<17);m_zoomOut->setEnabled(m_zoom>11);emit zoomChanged(m_centerLatitude,m_centerLongitude,m_zoom);
}

QPoint StationMapWidget::pointFor(double latitude,double longitude) const
{
    const QPointF center=worldPoint(m_centerLatitude,m_centerLongitude,m_zoom),point=worldPoint(latitude,longitude,m_zoom);
    const double scaleX=width()/600.0,scaleY=height()/300.0;
    return QPoint(qRound(width()/2.0+(point.x()-center.x())*scaleX),qRound(height()/2.0+(point.y()-center.y())*scaleY));
}

void StationMapWidget::rebuildPins()
{
    const auto old=findChildren<QToolButton*>(QString(),Qt::FindDirectChildrenOnly);for(auto *button:old)if(button->property("mapMarker").toBool())button->deleteLater();
    const QPoint myPoint=pointFor(m_userLatitude,m_userLongitude);if(rect().adjusted(10,10,-10,-10).contains(myPoint)){auto *me=new QToolButton(this);me->setProperty("mapMarker",true);me->setText(QStringLiteral("●"));me->setToolTip(QStringLiteral("我的位置"));me->setGeometry(myPoint.x()-13,myPoint.y()-13,26,26);me->setStyleSheet(QStringLiteral("QToolButton{background:#f59e0b;color:white;border:3px solid white;border-radius:13px;font-size:13px;}"));me->show();}
    for(int i=0;i<qMin(10,m_stations.size());++i){const auto station=m_stations[i];const QPoint point=pointFor(station.latitude,station.longitude);if(!rect().adjusted(16,16,-16,-16).contains(point))continue;const bool selected=station.id==m_selectedStationId;const int size=selected?36:30;auto *pin=new QToolButton(this);pin->setProperty("mapMarker",true);pin->setText(QString::number(i+1));pin->setToolTip(QStringLiteral("%1\n%2, %3").arg(station.name).arg(station.latitude,0,'f',6).arg(station.longitude,0,'f',6));pin->setGeometry(point.x()-size/2,point.y()-size,size,size);pin->setCursor(Qt::PointingHandCursor);pin->setStyleSheet(selected?QStringLiteral("QToolButton{background:#e11d48;color:white;border:4px solid white;border-radius:18px;font-weight:800;font-size:15px;}"):QStringLiteral("QToolButton{background:#0f766e;color:white;border:3px solid white;border-radius:15px;font-weight:700;}QToolButton:hover{background:#e11d48;}"));connect(pin,&QToolButton::clicked,this,[this,station]{m_selectedStationId=station.id;rebuildPins();emit stationFocused(station);});pin->show();}
    m_zoomIn->raise();m_zoomOut->raise();
}
