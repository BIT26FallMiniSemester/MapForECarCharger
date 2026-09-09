// 声明充电页的站点选择、电桩列表、订单操作和定时状态刷新接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef CHARGINGPAGE_H
#define CHARGINGPAGE_H

#include "models.h"

#include <QWidget>

// 创建用户端 API 客户端并连接 SocketClient 的成功/失败信号。
class ApiClient;
// 实现 QLabel 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QLabel;
class QButtonGroup;
class QGridLayout;
// 实现 QPushButton 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QPushButton;
// 实现 QTimer 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QTimer;
// 实现 QVBoxLayout 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QVBoxLayout;

// 创建充电页布局、订单操作按钮、站点电桩容器和状态刷新定时器。
class ChargingPage : public QWidget
{
    Q_OBJECT
public:
// 创建充电页布局、订单操作按钮、站点电桩容器和状态刷新定时器。
    explicit ChargingPage(ApiClient *api, QWidget *parent = nullptr);
// 选中指定站点并重新绘制地图标记。
    void selectStation(const StationSummary &station, double fromLatitude, double fromLongitude);
// 从后端恢复当前用户未完成订单。
    void restoreActiveOrder();
// 清除本地充电站、订单和电桩显示状态。
    void clearSession();
// 维护请求计数器并统一切换各页面的忙碌状态。
    void setBusy(bool busy);

signals:
    void activeOrderRestored(const ChargingOrder &order);

private:
// 实现 clearPiles 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void clearPiles();
// 根据当前订单状态更新操作按钮、指标和刷新定时器。
    void renderOrder();
// 显示操作提示，并按错误状态切换颜色。
    void setHint(const QString &text, bool error = false);
// 判断是否存在尚未完成或取消的订单。
    bool hasActiveOrder() const;
// 把协议状态转换为用户可读中文。
    QString statusText(const QString &status) const;

    ApiClient *m_api;
    StationSummary m_station;
    ChargingOrder m_order;
    double m_fromLatitude = 0;
    double m_fromLongitude = 0;
    QLabel *m_stationName = nullptr;
    QLabel *m_stationInfo = nullptr;
    QLabel *m_routeInfo = nullptr;
    QLabel *m_orderTitle = nullptr;
    QLabel *m_orderStatus = nullptr;
    QLabel *m_metrics = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_pileSummary = nullptr;
    QWidget *m_pileHost = nullptr;
    QVBoxLayout *m_pileLayout = nullptr;
    QGridLayout *m_pileGrid = nullptr;
    QButtonGroup *m_pileButtons = nullptr;
    QPushButton *m_reservePile = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_settle = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_refresh = nullptr;
    QTimer *m_timer = nullptr;
    QVector<ChargingPile> m_piles;
    int m_selectedPileIndex = -1;
};

#endif
