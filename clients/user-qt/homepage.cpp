// 实现首页地图、定位入口、范围控制、站点卡片和地图/列表联动。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "homepage.h"
#include "ui_homepage.h"
#include "stationmapwidget.h"

#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QtGlobal>
#include <QToolButton>

/// 创建首页 UI、地图区域、位置按钮、范围控件和站点卡片容器。
HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HomePage)
{
    ui->setupUi(this);
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->descLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->hintLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->radiusHint->setObjectName(QStringLiteral("subtitleLabel"));
    ui->locationButton->setObjectName(QStringLiteral("locationButton"));
    ui->locationButton->setStyleSheet(QStringLiteral("text-align: left; padding: 12px;"));
    ui->radiusUp->setObjectName(QStringLiteral("stepButton"));
    ui->radiusDown->setObjectName(QStringLiteral("stepButton"));
    ui->descLabel->setText(QStringLiteral("选择地图标记或站点卡片，查看详情后直接充电。"));

    setupRadiusControl();
    refreshLocationButton();

    auto *controls=new QGridLayout;controls->setHorizontalSpacing(8);controls->setVerticalSpacing(8);ui->homeLayout->removeWidget(ui->locationButton);ui->homeLayout->removeWidget(ui->radiusBox);ui->homeLayout->removeWidget(ui->queryButton);ui->radiusHint->setText(QStringLiteral("搜索范围"));ui->radiusUp->hide();ui->radiusDown->hide();controls->addWidget(ui->locationButton,0,0,1,2);controls->addWidget(ui->radiusBox,1,0);controls->addWidget(ui->queryButton,1,1);controls->setColumnStretch(0,1);controls->setColumnStretch(1,1);ui->homeLayout->insertLayout(2,controls);
    m_map = new StationMapWidget(this);
    m_map->setAccessibleName(QStringLiteral("附近充电站地图"));
    ui->homeLayout->insertWidget(3,m_map);
    auto *selection=new QFrame(this);selection->setObjectName(QStringLiteral("mapSelection"));auto *selectionLayout=new QHBoxLayout(selection);selectionLayout->setContentsMargins(12,8,10,8);
    m_mapDetail = new QLabel(QStringLiteral("点击地图标记或下方站点卡查看详情"),selection);m_mapDetail->setObjectName(QStringLiteral("mapDetail"));m_mapDetail->setWordWrap(true);
    m_mapCharge=new QPushButton(QStringLiteral("去充电"),selection);m_mapCharge->setMinimumSize(84,44);m_mapCharge->setCursor(Qt::PointingHandCursor);m_mapCharge->setEnabled(false);selectionLayout->addWidget(m_mapDetail,1);selectionLayout->addWidget(m_mapCharge);ui->homeLayout->insertWidget(4,selection);
    ui->homeLayout->setStretch(ui->homeLayout->indexOf(ui->stationScroll),1);
    connect(m_map,&StationMapWidget::stationFocused,this,&HomePage::focusStation);
    connect(m_map,&StationMapWidget::zoomChanged,this,&HomePage::mapZoomRequested);
    connect(m_mapCharge,&QPushButton::clicked,this,[this]{if(m_focusedStation.id>0)emit stationSelected(m_focusedStation);});

    connect(ui->locationButton, &QPushButton::clicked, this, &HomePage::changeLocationClicked);
    connect(ui->queryButton, &QPushButton::clicked, this, &HomePage::queryClicked);
}

HomePage::~HomePage()
{
    delete ui;
}

/// 返回当前站点搜索半径。
double HomePage::radiusKm() const
{
    return m_radiusKm;
}

double HomePage::mapCenterLatitude() const { return m_map->centerLatitude(); }
double HomePage::mapCenterLongitude() const { return m_map->centerLongitude(); }
int HomePage::mapZoom() const { return m_map->zoom(); }

/// 初始化范围下拉框和增减按钮的交互。
void HomePage::setupRadiusControl()
{
    ui->radiusCombo->setEditable(false);
    ui->radiusCombo->setInsertPolicy(QComboBox::NoInsert);
    ui->radiusCombo->setFocusPolicy(Qt::StrongFocus);
    ui->radiusCombo->setMaxVisibleItems(8);

    connect(ui->radiusCombo, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        const QString text = ui->radiusCombo->itemText(index);
        const int km = text.section(QLatin1Char(' '), 0, 0).toInt();
        if (km >= 1)
            setRadiusKm(km);
    });
    connect(ui->radiusUp, &QToolButton::clicked, this, [this]() {
        setRadiusKm(m_radiusKm + 1);
    });
    connect(ui->radiusDown, &QToolButton::clicked, this, [this]() {
        setRadiusKm(m_radiusKm - 1);
    });

    setRadiusKm(m_radiusKm);
}

/// 把搜索半径限制在 1 到 100 公里并同步控件显示。
void HomePage::setRadiusKm(int km)
{
    m_radiusKm = qBound(1, km, 100);
    const QString text = QStringLiteral("%1 km").arg(m_radiusKm);
    static const QStringList presets = {
        QStringLiteral("1 km"), QStringLiteral("5 km"), QStringLiteral("10 km"),
        QStringLiteral("20 km"), QStringLiteral("30 km"), QStringLiteral("50 km"),
        QStringLiteral("70 km"), QStringLiteral("100 km")
    };

    const QSignalBlocker blocker(ui->radiusCombo);
    ui->radiusCombo->clear();
    ui->radiusCombo->addItems(presets);
    int idx = ui->radiusCombo->findText(text);
    if (idx < 0) {
        ui->radiusCombo->insertItem(0, text);
        idx = 0;
    }
    ui->radiusCombo->setCurrentIndex(idx);

    ui->radiusDown->setEnabled(m_radiusKm > 1);
    ui->radiusUp->setEnabled(m_radiusKm < 100);
}

/// 维护请求计数器并统一切换各页面的忙碌状态。
void HomePage::setBusy(bool busy)
{
    ui->queryButton->setEnabled(!busy);
    ui->queryButton->setText(busy ? QStringLiteral("查询中…") : QStringLiteral("查询附近站点"));
}

/// 更新用户位置、地图中心点和位置按钮显示。
void HomePage::setLocation(double lat, double lng, const QString &displayName)
{
    m_lat = lat;
    m_lng = lng;
    m_displayName = displayName;
    m_map->setCenter(lat,lng);
    refreshLocationButton();
}

/// 将定位重置为北京市中心模拟坐标。
void HomePage::useSimulatedGps()
{
    setLocation(39.9042, 116.4074, QStringLiteral("北京市中心（可修改）"));
}

/// 在地图控件中显示腾讯静态地图 PNG。
void HomePage::showMap(const QByteArray &png){m_map->setImage(png);}
/// 地图失败时显示降级提示，同时保留站点列表。
void HomePage::showMapError(const QString &message){m_mapDetail->setText(QStringLiteral("地图暂时无法加载：%1。附近站点列表仍可使用。").arg(message));}

/// 同步地图选中项、详情标签、卡片状态和滚动位置。
void HomePage::focusStation(const StationSummary &station)
{
    m_focusedStation=station;m_map->selectStation(station.id);
    const QString price=station.priceCentsPerKwh>0?QStringLiteral("¥%1/度").arg(centsToYuanText(station.priceCentsPerKwh)):QStringLiteral("电价未公开");
    m_mapDetail->setText(QStringLiteral("%1\n%2 km · 空闲 %3/%4 · %5").arg(station.name).arg(station.distanceKm,0,'f',2).arg(station.availablePiles).arg(station.totalPiles).arg(price));
    m_mapDetail->setToolTip(QStringLiteral("%1\n坐标 %2, %3").arg(station.operatorName).arg(station.latitude,0,'f',6).arg(station.longitude,0,'f',6));
    m_mapCharge->setEnabled(station.availablePiles>0);m_mapCharge->setToolTip(station.availablePiles>0?QStringLiteral("进入充电界面"):QStringLiteral("该公共站点没有接入可控制电桩"));
    for(auto it=m_cards.begin();it!=m_cards.end();++it){it.value()->setProperty("selected",it.key()==station.id);it.value()->style()->unpolish(it.value());it.value()->style()->polish(it.value());}
    const qint64 stationId=station.id;QTimer::singleShot(0,this,[this,stationId]{if(auto *card=m_cards.value(stationId))ui->stationScroll->ensureWidgetVisible(card,0,12);});
}

/// 处理站点卡片和子控件的点击事件。
bool HomePage::eventFilter(QObject *watched,QEvent *event)
{
    if(event->type()==QEvent::MouseButtonRelease&&watched->property("stationId").isValid()){const qint64 id=watched->property("stationId").toLongLong();for(const auto &station:m_stations)if(station.id==id){focusStation(station);break;}return true;}
/// 处理站点卡片和子控件的点击事件。
    return QWidget::eventFilter(watched,event);
}

/// 实现 refreshLocationButton 的本地处理逻辑，保持与项目其他模块的接口约定一致。
void HomePage::refreshLocationButton()
{
    ui->locationButton->setText(QStringLiteral("我的位置 · %1  ›").arg(m_displayName));
}

/// 更新首页提示文本。
void HomePage::showHint(const QString &text)
{
    ui->hintLabel->setText(text);
}

/// 删除旧的站点卡片及对应布局项。
void HomePage::clearCards()
{
    m_cards.clear();
    while (ui->stationListLayout->count() > 1) {
        QLayoutItem *item = ui->stationListLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

/// 把站点结果渲染为地图标记和站点卡片，并选中首项。
void HomePage::showStations(const QVector<StationSummary> &stations)
{
    clearCards();
    m_stations=stations;
    m_map->setStations(stations);
    if (stations.isEmpty()) {
        m_focusedStation=StationSummary{};m_mapCharge->setEnabled(false);m_mapDetail->setText(QStringLiteral("当前范围内没有站点"));
        showHint(QStringLiteral("附近没有充电站，可加大范围或换一个位置。"));
        return;
    }
    showHint(QStringLiteral("共 %1 座充电站，已按距离由近到远排序").arg(stations.size()));

    for (int rank=0;rank<stations.size();++rank) { const StationSummary &s=stations[rank];
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("stationCard"));card->setProperty("stationId",s.id);card->setProperty("selected",false);card->setCursor(Qt::PointingHandCursor);card->installEventFilter(this);m_cards.insert(s.id,card);
        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(14, 12, 14, 12);

        auto *header=new QHBoxLayout;auto *name = new QLabel(QStringLiteral("%1  %2").arg(rank+1).arg(s.name));
        name->setObjectName(QStringLiteral("cardTitle"));name->setWordWrap(true);name->setProperty("stationId",s.id);name->setCursor(Qt::PointingHandCursor);name->installEventFilter(this);

        const QString price = s.priceCentsPerKwh>0?QStringLiteral("¥%1/度").arg(centsToYuanText(s.priceCentsPerKwh)):QStringLiteral("电价未公开");
        const QString availability=s.totalPiles>0?QStringLiteral("空闲 %1/%2").arg(s.availablePiles).arg(s.totalPiles):QStringLiteral("空闲状态未公开");
        auto *info = new QLabel(
            QStringLiteral("%1 · %2 · 快充 %3 / 慢充 %4\n%5")
                .arg(price)
                .arg(availability)
                .arg(s.fastConnectorCount).arg(s.slowConnectorCount)
                .arg(s.address));
        info->setObjectName(QStringLiteral("cardInfo"));
        info->setWordWrap(true);info->setProperty("stationId",s.id);info->setCursor(Qt::PointingHandCursor);info->installEventFilter(this);

        auto *choose = new QPushButton(QStringLiteral("去充电"));
        choose->setMinimumSize(76,44);
        choose->setCursor(Qt::PointingHandCursor);
        choose->setEnabled(s.availablePiles > 0);
        choose->setToolTip(choose->isEnabled()?QStringLiteral("进入充电界面"):QStringLiteral("该公共站点没有接入可控制电桩"));
        connect(choose, &QPushButton::clicked, this, [this, s]() {
            emit stationSelected(s);
        });
        auto *distance = new QLabel(QStringLiteral("<a href=\"route\">%1 km · 查看路线</a>")
            .arg(s.distanceKm, 0, 'f', 2));
        distance->setTextFormat(Qt::RichText);
        distance->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
        distance->setOpenExternalLinks(false);
        distance->setToolTip(QStringLiteral("查看到 %1 的导航路线").arg(s.name));
        connect(distance, &QLabel::linkActivated, this, [this, s] { emit navigationRequested(s); });

        header->addWidget(name,1);header->addWidget(choose);box->addLayout(header);
        box->addWidget(info);
        box->addWidget(distance);
        ui->stationListLayout->insertWidget(ui->stationListLayout->count() - 1, card);
    }
    focusStation(stations.first());
}
