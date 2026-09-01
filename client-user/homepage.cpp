#include "homepage.h"
#include "ui_homepage.h"

#include <QComboBox>
#include <QFrame>
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
    refreshLocationButton();
}

void HomePage::useSimulatedGps()
{
    setLocation(39.9042, 116.4074, QStringLiteral("北京市东城区（模拟定位）"));
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
    if (stations.isEmpty()) {
        showHint(QStringLiteral("附近没有充电站，可加大范围或换一个位置。"));
        return;
    }
    showHint(QStringLiteral("共 %1 座充电站，已按距离由近到远排序").arg(stations.size()));

    for (const StationSummary &s : stations) {
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("card"));
        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(14, 12, 14, 12);

        auto *name = new QLabel(s.name);
        name->setObjectName(QStringLiteral("cardTitle"));

        const QString price = centsToYuanText(s.priceCentsPerKwh) + QStringLiteral(" 元/度");
        auto *info = new QLabel(
            QStringLiteral("%1  ·  空闲 %2 / %3  ·  %4 km\n%5")
                .arg(price)
                .arg(s.availablePiles)
                .arg(s.totalPiles)
                .arg(s.distanceKm, 0, 'f', 2)
                .arg(s.address));
        info->setObjectName(QStringLiteral("cardInfo"));
        info->setWordWrap(true);

        box->addWidget(name);
        box->addWidget(info);
        ui->stationListLayout->insertWidget(ui->stationListLayout->count() - 1, card);
    }
}
