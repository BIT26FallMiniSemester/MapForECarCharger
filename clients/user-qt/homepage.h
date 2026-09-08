// 声明用户端首页、地图、位置、搜索范围和站点卡片交互接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include "models.h"

#include <QByteArray>
#include <QHash>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
// 创建首页 UI、地图区域、位置按钮、范围控件和站点卡片容器。
class HomePage;
}
QT_END_NAMESPACE

// 实现 QEvent 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QEvent;
// 实现 QFrame 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QFrame;
// 实现 QLabel 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QLabel;
// 实现 QPushButton 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QPushButton;

// 创建首页 UI、地图区域、位置按钮、范围控件和站点卡片容器。
class HomePage : public QWidget
{
    Q_OBJECT

public:
// 创建首页 UI、地图区域、位置按钮、范围控件和站点卡片容器。
    explicit HomePage(QWidget *parent = nullptr);
// 创建首页 UI、地图区域、位置按钮、范围控件和站点卡片容器。
    ~HomePage();

    double latitude() const { return m_lat; }
    double longitude() const { return m_lng; }
// 返回当前站点搜索半径。
    double radiusKm() const;
    QString displayName() const { return m_displayName; }
    double mapCenterLatitude() const;
    double mapCenterLongitude() const;
    int mapZoom() const;

// 维护请求计数器并统一切换各页面的忙碌状态。
    void setBusy(bool busy);
// 把站点结果渲染为地图标记和站点卡片，并选中首项。
    void showStations(const QVector<StationSummary> &stations);
// 更新首页提示文本。
    void showHint(const QString &text);
// 更新用户位置、地图中心点和位置按钮显示。
    void setLocation(double lat, double lng, const QString &displayName);
// 在地图控件中显示腾讯静态地图 PNG。
    void showMap(const QByteArray &png);
// 地图失败时显示降级提示，同时保留站点列表。
    void showMapError(const QString &message);
// 将定位重置为北京市中心模拟坐标。
    void useSimulatedGps();

signals:
// 实现 queryClicked 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void queryClicked();
// 实现 changeLocationClicked 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void changeLocationClicked();
// 实现 stationSelected 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void stationSelected(const StationSummary &station);
    void navigationRequested(const StationSummary &station);
// 实现 mapZoomRequested 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void mapZoomRequested(double centerLatitude, double centerLongitude, int zoom);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
// 实现 refreshLocationButton 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void refreshLocationButton();
// 初始化范围下拉框和增减按钮的交互。
    void setupRadiusControl();
// 把搜索半径限制在 1 到 100 公里并同步控件显示。
    void setRadiusKm(int km);
// 删除旧的站点卡片及对应布局项。
    void clearCards();
// 同步地图选中项、详情标签、卡片状态和滚动位置。
    void focusStation(const StationSummary &station);

    Ui::HomePage *ui;
    double m_lat = 39.9042;
    double m_lng = 116.4074;
    int m_radiusKm = 10;
// 实现 QStringLiteral 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    QString m_displayName = QStringLiteral("北京市中心（可修改）");
// 创建地图背景、缩放按钮并准备绘制站点标记。
    class StationMapWidget *m_map = nullptr;
    QLabel *m_mapDetail = nullptr;
    QPushButton *m_mapCharge = nullptr;
    QVector<StationSummary> m_stations;
    QHash<qint64,QFrame*> m_cards;
    StationSummary m_focusedStation;
};

#endif
