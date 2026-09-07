// 声明静态地图控件、缩放、站点标记和地图缩放事件接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef STATIONMAPWIDGET_H
#define STATIONMAPWIDGET_H

#include "models.h"

#include <QPixmap>
#include <QVector>
#include <QWidget>

// 实现 QLabel 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QLabel;
// 实现 QToolButton 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QToolButton;

// 创建地图背景、缩放按钮并准备绘制站点标记。
class StationMapWidget : public QWidget
{
    Q_OBJECT
public:
// 创建地图背景、缩放按钮并准备绘制站点标记。
    explicit StationMapWidget(QWidget *parent = nullptr);
// 更新地图中心坐标、缩放级别和站点标记。
    void setCenter(double latitude, double longitude, int zoom = 14);
// 把静态地图 PNG 设置为地图背景并刷新标记。
    void setImage(const QByteArray &png);
// 保存站点集合并按需初始化选中项。
    void setStations(const QVector<StationSummary> &stations);
// 选中指定站点并重新绘制地图标记。
    void selectStation(qint64 stationId);
signals:
// 实现 stationFocused 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void stationFocused(const StationSummary &station);
// 实现 zoomChanged 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void zoomChanged(double centerLatitude, double centerLongitude, int zoom);
private:
// 根据当前投影位置重建用户位置和前十个站点标记。
    void rebuildPins();
// 把经纬度通过 Web Mercator 投影换算为控件像素坐标。
    QPoint pointFor(double latitude, double longitude) const;
// 调整缩放级别、以选中站点为中心并请求新地图。
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
