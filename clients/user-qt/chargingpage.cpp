// 实现电桩选择、预约/开始/结束/支付/取消订单和充电数据轮询展示。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "chargingpage.h"

#include "apiclient.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString durationText(qint64 seconds)
{
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(seconds % 3600 / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
}

/// 创建充电页布局、订单操作按钮、站点电桩容器和状态刷新定时器。
ChargingPage::ChargingPage(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api), m_timer(new QTimer(this))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 20, 16, 12);
    root->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("当前充电"));
    title->setObjectName(QStringLiteral("titleLabel"));
    auto *subtitle = new QLabel(QStringLiteral("选择空闲电桩后预约；开始充电后费用由 Qt 后端实时计算。"));
    subtitle->setObjectName(QStringLiteral("subtitleLabel"));
    subtitle->setWordWrap(true);
    root->addWidget(title);
    root->addWidget(subtitle);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);

    auto *stationCard = new QFrame;
    stationCard->setObjectName(QStringLiteral("card"));
    auto *stationLayout = new QVBoxLayout(stationCard);
    m_stationName = new QLabel(QStringLiteral("尚未选择充电站"));
    m_stationName->setObjectName(QStringLiteral("cardTitle"));
    m_stationInfo = new QLabel(QStringLiteral("请从首页选择一个有空闲电桩的站点"));
    m_stationInfo->setObjectName(QStringLiteral("cardInfo"));
    m_stationInfo->setWordWrap(true);
    m_routeInfo = new QLabel;
    m_routeInfo->setObjectName(QStringLiteral("routeInfo"));
    stationLayout->addWidget(m_stationName);
    stationLayout->addWidget(m_stationInfo);
    stationLayout->addWidget(m_routeInfo);
    contentLayout->addWidget(stationCard);

    m_pileHost = new QFrame;
    m_pileHost->setObjectName(QStringLiteral("card"));
    m_pileLayout = new QVBoxLayout(m_pileHost);
    auto *pileHeading = new QLabel(QStringLiteral("选择电桩"));
    pileHeading->setObjectName(QStringLiteral("cardTitle"));
    m_pileSummary = new QLabel(QStringLiteral("请先从首页选择充电站"));
    m_pileSummary->setObjectName(QStringLiteral("cardInfo"));
    m_pileSelector = new QComboBox;
    m_pileSelector->setMaxVisibleItems(10);
    m_pileSelector->setAccessibleName(QStringLiteral("电桩选择"));
    m_reservePile = new QPushButton(QStringLiteral("预约所选电桩"));
    m_reservePile->setEnabled(false);
    m_pileLayout->addWidget(pileHeading);
    m_pileLayout->addWidget(m_pileSummary);
    m_pileLayout->addWidget(m_pileSelector);
    m_pileLayout->addWidget(m_reservePile);
    contentLayout->addWidget(m_pileHost);

    auto *orderCard = new QFrame;
    orderCard->setObjectName(QStringLiteral("chargeCard"));
    auto *orderLayout = new QVBoxLayout(orderCard);
    m_orderTitle = new QLabel(QStringLiteral("暂无进行中的订单"));
    m_orderTitle->setObjectName(QStringLiteral("cardTitle"));
    m_orderStatus = new QLabel(QStringLiteral("选择站点和电桩后开始"));
    m_orderStatus->setObjectName(QStringLiteral("chargeStatus"));
    m_metrics = new QLabel(QStringLiteral("时长 00:00:00\n电量 0.000 kWh\n金额 ¥0.00"));
    m_metrics->setObjectName(QStringLiteral("chargeMetrics"));
    auto *actions = new QGridLayout;
    actions->setHorizontalSpacing(8);
    actions->setVerticalSpacing(8);
    m_start = new QPushButton(QStringLiteral("开始充电"));
    m_stop = new QPushButton(QStringLiteral("结束充电"));
    m_stop->setObjectName(QStringLiteral("dangerButton"));
    m_settle = new QPushButton(QStringLiteral("余额付款"));
    m_cancel = new QPushButton(QStringLiteral("取消预约"));
    m_cancel->setObjectName(QStringLiteral("secondaryButton"));
    m_refresh = new QPushButton(QStringLiteral("刷新状态"));
    m_refresh->setObjectName(QStringLiteral("secondaryButton"));
    actions->addWidget(m_start, 0, 0, 1, 2);
    actions->addWidget(m_stop, 0, 0, 1, 2);
    actions->addWidget(m_settle, 0, 0, 1, 2);
    actions->addWidget(m_cancel, 1, 0);
    actions->addWidget(m_refresh, 1, 1);
    actions->setColumnStretch(0, 1);
    actions->setColumnStretch(1, 1);
    orderLayout->addWidget(m_orderTitle);
    orderLayout->addWidget(m_orderStatus);
    orderLayout->addWidget(m_metrics);
    orderLayout->addLayout(actions);
    contentLayout->addWidget(orderCard);

    m_hint = new QLabel;
    m_hint->setObjectName(QStringLiteral("subtitleLabel"));
    m_hint->setWordWrap(true);
    contentLayout->addWidget(m_hint);
    contentLayout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll);

    connect(m_timer, &QTimer::timeout, this, [this] {
        if (m_order.id > 0 && m_order.status == QStringLiteral("CHARGING"))
            m_api->fetchOrder(m_order.id);
    });
    m_timer->setInterval(2000);

    connect(m_start, &QPushButton::clicked, this, [this] { m_api->startOrder(m_order.id); });
    connect(m_stop, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("结束充电"),
                                  QStringLiteral("结束后将生成待支付账单，确定结束吗？")) == QMessageBox::Yes)
            m_api->stopOrder(m_order.id);
    });
    connect(m_settle, &QPushButton::clicked, this, [this] { m_api->settleOrder(m_order.id); });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("取消订单"),
                                  QStringLiteral("确定取消当前预约吗？")) == QMessageBox::Yes)
            m_api->cancelOrder(m_order.id);
    });
    connect(m_refresh, &QPushButton::clicked, this, [this] {
        if (m_order.id > 0)
            m_api->fetchOrder(m_order.id);
        else
            m_api->fetchActiveOrder();
    });
    connect(m_pileSelector, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
        m_reservePile->setEnabled(index >= 0 && index < m_piles.size() &&
                                  m_piles[index].status == QStringLiteral("IDLE"));
    });
    connect(m_reservePile, &QPushButton::clicked, this, [this] {
        const int index = m_pileSelector->currentIndex();
        if (index < 0 || index >= m_piles.size() ||
            m_piles[index].status != QStringLiteral("IDLE"))
            return;
        const ChargingPile pile = m_piles[index];
        if (QMessageBox::question(this, QStringLiteral("预约电桩"),
                QStringLiteral("确定预约 %1 吗？创建后将自动锁定该电桩 15 分钟。")
                    .arg(pile.pileNo)) == QMessageBox::Yes) {
            m_reservePile->setEnabled(false);
            setHint(QStringLiteral("正在创建订单并预约电桩…"));
            m_api->createOrder(m_station.id, pile.id);
        }
    });

    connect(m_api, &ApiClient::stationPilesReady, this,
            [this](qint64 stationId, const QVector<ChargingPile> &piles) {
        if (stationId != m_station.id)
            return;
        clearPiles();
        m_piles = piles;
        int available = 0;
        int firstAvailable = -1;
        for (int i = 0; i < piles.size(); ++i) {
            const ChargingPile &pile = piles[i];
            const QString type = pile.chargeType == QStringLiteral("FAST")
                                     ? QStringLiteral("快充") : QStringLiteral("慢充");
            m_pileSelector->addItem(QStringLiteral("%1 · %2 · %3 kW · %4")
                .arg(pile.pileNo, type)
                .arg(pile.ratedPowerW / 1000.0, 0, 'f', 1)
                .arg(statusText(pile.status)));
            if (pile.status == QStringLiteral("IDLE")) {
                if (firstAvailable < 0)
                    firstAvailable = i;
                ++available;
            } else if (auto *model = qobject_cast<QStandardItemModel *>(m_pileSelector->model())) {
                if (QStandardItem *item = model->item(i))
                    item->setEnabled(false);
            }
        }
        m_pileSummary->setText(QStringLiteral("共 %1 个电桩，当前空闲 %2 个").arg(piles.size()).arg(available));
        if (firstAvailable >= 0)
            m_pileSelector->setCurrentIndex(firstAvailable);
        m_reservePile->setEnabled(firstAvailable >= 0);
        if (piles.isEmpty() || available == 0)
            setHint(QStringLiteral("该站点当前没有空闲电桩，请返回首页选择其他站点。"), true);
    });

    connect(m_api, &ApiClient::activeOrderReady, this,
            [this](bool hasOrder, const ChargingOrder &order) {
        m_order = hasOrder ? order : ChargingOrder{};
        if (hasOrder) {
            m_station.id = order.stationId;
            m_stationName->setText(order.stationName);
            m_stationInfo->setText(QStringLiteral("电桩 %1 · %2 kW · ¥%3/度")
                .arg(order.pileNo)
                .arg(order.ratedPowerW / 1000.0, 0, 'f', 1)
                .arg(centsToYuanText(order.priceCentsPerKwh)));
            m_routeInfo->setText(QStringLiteral("这是后端恢复的当前订单"));
        }
        renderOrder();
        if (hasOrder) {
            setHint(QStringLiteral("已恢复后端保存的未完成订单。"));
            emit activeOrderRestored(order);
        }
    });
    connect(m_api, &ApiClient::orderReady, this,
            [this](const QString &operation, const ChargingOrder &order) {
        m_order = order;
        renderOrder();
        if (operation == QStringLiteral("create")) {
            setHint(QStringLiteral("订单已创建，正在锁定电桩…"));
            m_api->reserveOrder(order.id);
        } else if (operation == QStringLiteral("reserve")) {
            setHint(QStringLiteral("预约成功，请在 15 分钟内开始充电。"));
        } else if (operation == QStringLiteral("start")) {
            setHint(QStringLiteral("充电已开始，数据每 2 秒从后端刷新。"));
        } else if (operation == QStringLiteral("stop")) {
            setHint(QStringLiteral("充电已结束，请使用余额支付账单。"));
        } else if (operation == QStringLiteral("settle")) {
            setHint(QStringLiteral("支付完成，管理端收入与订单统计已同步更新。"));
        } else if (operation == QStringLiteral("cancel")) {
            setHint(QStringLiteral("订单已取消，电桩已释放。"));
        }
    });
    connect(m_api, &ApiClient::routeReady, this, [this](const RouteInfo &route) {
        m_routeInfo->setText(QStringLiteral("腾讯地图驾车路线 · %1 km · 约 %2 分钟")
            .arg(route.distanceMeters / 1000.0, 0, 'f', 1)
            .arg(qMax<qint64>(1, route.durationSeconds / 60)));
    });
    connect(m_api, &ApiClient::requestFailed, this,
            [this](const QString &context, int, const QString &message) {
        if (context.startsWith(QStringLiteral("order:")) ||
            context.startsWith(QStringLiteral("piles:")) || context == QStringLiteral("route"))
            setHint(message, true);
    });

    clearPiles();
    renderOrder();
}

/// 选中指定站点并重新绘制地图标记。
void ChargingPage::selectStation(const StationSummary &station, double fromLatitude, double fromLongitude)
{
    if (hasActiveOrder()) {
        setHint(QStringLiteral("请先完成或取消当前订单，再选择其他电桩。"), true);
        return;
    }
    m_order = ChargingOrder{};
    m_station = station;
    m_fromLatitude = fromLatitude;
    m_fromLongitude = fromLongitude;
    m_stationName->setText(station.name);
    m_stationInfo->setText(QStringLiteral("%1\n¥%2/度 · 空闲 %3/%4")
        .arg(station.address, centsToYuanText(station.priceCentsPerKwh))
        .arg(station.availablePiles).arg(station.totalPiles));
    m_routeInfo->setText(QStringLiteral("正在获取腾讯地图驾车路线…"));
    clearPiles();
    renderOrder();
    m_api->fetchStationPiles(station.id);
    m_api->planRoute(fromLatitude, fromLongitude, station.latitude, station.longitude);
}

/// 从后端恢复当前用户未完成订单。
void ChargingPage::restoreActiveOrder()
{
    m_api->fetchActiveOrder();
}

/// 清除本地充电站、订单和电桩显示状态。
void ChargingPage::clearSession()
{
    m_timer->stop();
    m_station = StationSummary{};
    m_order = ChargingOrder{};
    clearPiles();
    renderOrder();
}

/// 维护请求计数器并统一切换各页面的忙碌状态。
void ChargingPage::setBusy(bool busy)
{
    m_start->setEnabled(!busy && m_order.status == QStringLiteral("RESERVED"));
    m_stop->setEnabled(!busy && m_order.status == QStringLiteral("CHARGING"));
    m_settle->setEnabled(!busy && m_order.status == QStringLiteral("UNPAID"));
    m_cancel->setEnabled(!busy && (m_order.status == QStringLiteral("PENDING") ||
                                   m_order.status == QStringLiteral("RESERVED")));
    m_refresh->setEnabled(!busy);
    const int index = m_pileSelector->currentIndex();
    m_reservePile->setEnabled(!busy && index >= 0 && index < m_piles.size() &&
                              m_piles[index].status == QStringLiteral("IDLE"));
}

/// 实现 clearPiles 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void ChargingPage::clearPiles()
{
    m_piles.clear();
    m_pileSelector->clear();
    m_pileSummary->setText(QStringLiteral("正在读取电桩状态…"));
    m_reservePile->setEnabled(false);
    m_pileHost->setVisible(m_station.id > 0 && m_order.id == 0);
}

/// 根据当前订单状态更新操作按钮、指标和刷新定时器。
void ChargingPage::renderOrder()
{
    const bool exists = m_order.id > 0;
    m_orderTitle->setText(exists ? QStringLiteral("订单 %1").arg(m_order.orderNo)
                                 : QStringLiteral("暂无进行中的订单"));
    m_orderStatus->setText(exists
        ? QStringLiteral("%1 · %2 · %3").arg(statusText(m_order.status), m_order.stationName, m_order.pileNo)
        : QStringLiteral("从首页选择站点和空闲电桩"));
    m_metrics->setText(QStringLiteral("时长 %1\n电量 %2 kWh\n金额 ¥%3")
        .arg(durationText(m_order.durationSeconds))
        .arg(m_order.energyWh / 1000.0, 0, 'f', 3)
        .arg(centsToYuanText(m_order.amountCents)));
    m_metrics->setVisible(exists);
    m_start->setVisible(m_order.status == QStringLiteral("RESERVED"));
    m_stop->setVisible(m_order.status == QStringLiteral("CHARGING"));
    m_settle->setVisible(m_order.status == QStringLiteral("UNPAID"));
    m_cancel->setVisible(m_order.status == QStringLiteral("PENDING") ||
                         m_order.status == QStringLiteral("RESERVED"));
    m_refresh->setVisible(exists && m_order.status != QStringLiteral("COMPLETED") &&
                          m_order.status != QStringLiteral("CANCELLED"));
    m_pileHost->setVisible(m_station.id > 0 && m_order.id == 0);
    if (m_order.status == QStringLiteral("CHARGING"))
        m_timer->start();
    else
        m_timer->stop();
    setBusy(false);
}

/// 显示操作提示，并按错误状态切换颜色。
void ChargingPage::setHint(const QString &text, bool error)
{
    m_hint->setObjectName(error ? QStringLiteral("statusLabel") : QStringLiteral("subtitleLabel"));
    m_hint->setStyleSheet(error ? QStringLiteral("color:#dc2626") : QString());
    m_hint->setText(text);
}

/// 判断是否存在尚未完成或取消的订单。
bool ChargingPage::hasActiveOrder() const
{
    return m_order.id > 0 && m_order.status != QStringLiteral("COMPLETED") &&
           m_order.status != QStringLiteral("CANCELLED");
}

/// 把协议状态转换为用户可读中文。
QString ChargingPage::statusText(const QString &status) const
{
    if (status == QStringLiteral("IDLE")) return QStringLiteral("空闲");
    if (status == QStringLiteral("PENDING")) return QStringLiteral("待预约");
    if (status == QStringLiteral("RESERVED")) return QStringLiteral("已预约");
    if (status == QStringLiteral("CHARGING")) return QStringLiteral("充电中");
    if (status == QStringLiteral("UNPAID")) return QStringLiteral("待支付");
    if (status == QStringLiteral("COMPLETED")) return QStringLiteral("已完成");
    if (status == QStringLiteral("CANCELLED")) return QStringLiteral("已取消");
    if (status == QStringLiteral("FAULT")) return QStringLiteral("故障");
    if (status == QStringLiteral("OFFLINE")) return QStringLiteral("离线");
    return status;
}
