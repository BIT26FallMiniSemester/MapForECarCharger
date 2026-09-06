#include "locationdialog.h"
#include "ui_locationdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVariant>

LocationDialog::LocationDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LocationDialog)
{
    ui->setupUi(this);
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->descLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->regionHint->setObjectName(QStringLiteral("subtitleLabel"));
    ui->addressHint->setObjectName(QStringLiteral("subtitleLabel"));
    ui->statusLabel->setObjectName(QStringLiteral("statusLabel"));
    ui->useRegionButton->setObjectName(QStringLiteral("secondaryButton"));

    ui->keyHint->hide();
    ui->keyEdit->hide();
    ui->locateMeButton->hide();
    ui->addressEdit->hide();
    ui->searchButton->hide();
    ui->addressHint->setText(QStringLiteral("演示环境可直接选择预设区域"));

    fillRegions();

    connect(ui->useRegionButton, &QPushButton::clicked, this, &LocationDialog::onUseRegion);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &LocationDialog::onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &LocationDialog::reject);
}

LocationDialog::~LocationDialog()
{
    delete ui;
}

void LocationDialog::setCurrentLocation(double lat, double lng, const QString &displayName)
{
    m_lat = lat;
    m_lng = lng;
    m_displayName = displayName;
    ui->statusLabel->setText(QStringLiteral("当前：%1").arg(displayName));
}

void LocationDialog::fillRegions()
{
    struct Region {
        const char *name;
        double lat;
        double lng;
        const char *display;
    };
    static const Region kRegions[] = {
        {"模拟定位 · 天安门附近", 39.9042, 116.4074, "北京市东城区（模拟定位）"},
        {"东城区", 39.9284, 116.4164, "北京市东城区"},
        {"西城区", 39.9123, 116.3660, "北京市西城区"},
        {"朝阳区", 39.9215, 116.4431, "北京市朝阳区"},
        {"海淀区", 39.9590, 116.2981, "北京市海淀区"},
        {"丰台区", 39.8585, 116.2867, "北京市丰台区"},
        {"通州区", 39.9099, 116.6564, "北京市通州区"},
        {"昌平区", 40.2181, 116.2347, "北京市昌平区"},
    };

    ui->regionCombo->clear();
    for (const Region &r : kRegions) {
        QVariantMap data;
        data.insert(QStringLiteral("lat"), r.lat);
        data.insert(QStringLiteral("lng"), r.lng);
        data.insert(QStringLiteral("display"), QString::fromUtf8(r.display));
        ui->regionCombo->addItem(QString::fromUtf8(r.name), data);
    }
}

void LocationDialog::applyLocation(double lat, double lng, const QString &name)
{
    m_lat = lat;
    m_lng = lng;
    m_displayName = name;
    ui->statusLabel->setText(QStringLiteral("已选择：%1").arg(name));
}

void LocationDialog::onUseRegion()
{
    const QVariantMap data = ui->regionCombo->currentData().toMap();
    applyLocation(data.value(QStringLiteral("lat")).toDouble(),
                  data.value(QStringLiteral("lng")).toDouble(),
                  data.value(QStringLiteral("display")).toString());
}

void LocationDialog::onAccepted()
{
    accept();
}
