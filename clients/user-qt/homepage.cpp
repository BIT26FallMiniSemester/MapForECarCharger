#include "homepage.h"
#include "ui_homepage.h"
#include "stationmapwidget.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QtGlobal>
#include <QToolButton>

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

    setupRadiusControl();
    refreshLocationButton();

    m_map = new StationMapWidget(this);
    ui->homeLayout->insertWidget(2,m_map,0,Qt::AlignHCenter);
    m_mapDetail = new QLabel(QStringLiteral("橙色圆点是当前位置；编号标记对应下方推荐列表"),this);
    m_mapDetail->setObjectName(QStringLiteral("mapDetail"));m_mapDetail->setWordWrap(true);m_mapDetail->setMinimumHeight(46);ui->homeLayout->insertWidget(3,m_mapDetail);
    connect(m_map,&StationMapWidget::stationFocused,this,&HomePage::focusStation);

    connect(ui->locationButton, &QPushButton::clicked, this, &HomePage::changeLocationClicked);
    connect(ui->queryButton, &QPushButton::clicked, this, &HomePage::queryClicked);
}

HomePage::~HomePage()
{
    delete ui;
}

double HomePage::radiusKm() const
{
    return m_radiusKm;
}

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

void HomePage::setBusy(bool busy)
{
    setEnabled(!busy);
}

void HomePage::setLocation(double lat, double lng, const QString &displayName)
{
    m_lat = lat;
    m_lng = lng;
    m_displayName = displayName;
    m_map->setCenter(lat,lng);
    refreshLocationButton();
}

void HomePage::useSimulatedGps()
{
    setLocation(39.9042, 116.4074, QStringLiteral("北京市中心（可修改）"));
}

void HomePage::showMap(const QByteArray &png){m_map->setImage(png);}
void HomePage::showMapError(const QString &message){m_mapDetail->setText(QStringLiteral("地图暂时无法加载：%1。附近站点列表仍可使用。").arg(message));}

void HomePage::focusStation(const StationSummary &station)
{
    const QString price=station.priceCentsPerKwh>0?QStringLiteral("测试计费 ¥%1/度").arg(centsToYuanText(station.priceCentsPerKwh)):QStringLiteral("电价未公开");
    m_mapDetail->setText(QStringLiteral("%1\n%2 · %3 · 快充 %4 / 慢充 %5 · %6 km · %7").arg(station.name,station.district,station.operatorName).arg(station.fastConnectorCount).arg(station.slowConnectorCount).arg(station.distanceKm,0,'f',2).arg(price));
}

void HomePage::refreshLocationButton()
{
    ui->locationButton->setText(QStringLiteral("我的位置  ›\n%1").arg(m_displayName));
}

void HomePage::showHint(const QString &text)
{
    ui->hintLabel->setText(text);
}

void HomePage::clearCards()
{
    while (ui->stationListLayout->count() > 1) {
        QLayoutItem *item = ui->stationListLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

void HomePage::showStations(const QVector<StationSummary> &stations)
{
    clearCards();
    m_map->setStations(stations);
    if (stations.isEmpty()) {
        showHint(QStringLiteral("附近没有充电站，可加大范围或换一个位置。"));
        return;
    }
    focusStation(stations.first());
    showHint(QStringLiteral("共 %1 座充电站，已按距离由近到远排序").arg(stations.size()));

    for (int rank=0;rank<stations.size();++rank) { const StationSummary &s=stations[rank];
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("card"));
        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(14, 12, 14, 12);

        auto *name = new QLabel(QStringLiteral("%1  %2").arg(rank+1).arg(s.name));
        name->setObjectName(QStringLiteral("cardTitle"));

        const QString price = s.priceCentsPerKwh>0?centsToYuanText(s.priceCentsPerKwh)+QStringLiteral(" 元/度（测试）"):QStringLiteral("电价未公开");
        const QString availability=s.totalPiles>0?QStringLiteral("测试桩空闲 %1 / %2").arg(s.availablePiles).arg(s.totalPiles):QStringLiteral("实时空闲状态未公开");
        auto *info = new QLabel(
            QStringLiteral("%1  ·  %2  ·  %3 km\n%4 · %5 · 快充 %6 / 慢充 %7")
                .arg(price)
                .arg(availability)
                .arg(s.distanceKm, 0, 'f', 2)
                .arg(s.address,s.operatorName)
                .arg(s.fastConnectorCount).arg(s.slowConnectorCount));
        info->setObjectName(QStringLiteral("cardInfo"));
        info->setWordWrap(true);

        auto *actions=new QHBoxLayout;auto *locate=new QPushButton(QStringLiteral("在地图查看"));locate->setObjectName(QStringLiteral("secondaryButton"));connect(locate,&QPushButton::clicked,this,[this,s]{focusStation(s);});
        auto *choose = new QPushButton(QStringLiteral("使用课程测试桩"));
        choose->setCursor(Qt::PointingHandCursor);
        choose->setEnabled(s.availablePiles > 0);
        if (!choose->isEnabled())
            choose->setText(QStringLiteral("仅查看站点"));
        connect(choose, &QPushButton::clicked, this, [this, s]() {
            emit stationSelected(s);
        });

        box->addWidget(name);
        box->addWidget(info);
        actions->addWidget(locate);actions->addWidget(choose);box->addLayout(actions);
        ui->stationListLayout->insertWidget(ui->stationListLayout->count() - 1, card);
    }
}
