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
    ui->addressHint->setText(QStringLiteral("或输入详细地址（由后端调用腾讯地图）"));

    fillRegions();

    connect(ui->useRegionButton, &QPushButton::clicked, this, &LocationDialog::onUseRegion);
    connect(ui->searchButton, &QPushButton::clicked, this, &LocationDialog::onSearch);
    connect(ui->addressEdit, &QLineEdit::returnPressed, this, &LocationDialog::onSearch);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &LocationDialog::onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &LocationDialog::reject);
}

void LocationDialog::applyGeocodedLocation(double lat, double lng, const QString &displayName)
{
    m_searching = false;
    ui->searchButton->setEnabled(true);
    ui->searchButton->setText(QStringLiteral("搜索位置"));
    applyLocation(lat, lng, displayName);
}

void LocationDialog::showSearchError(const QString &message)
{
    m_searching = false;
    ui->searchButton->setEnabled(true);
    ui->searchButton->setText(QStringLiteral("搜索位置"));
    ui->statusLabel->setText(message);
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
        {"北京市中心", 39.9042, 116.4074, "北京市中心"},
        {"示例位置 · 王府井", 39.910128, 116.409354, "北京市东城区王府井霞公府公共充电站"},
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

void LocationDialog::onSearch()
{
    const QString address = ui->addressEdit->text().trimmed();
    if (address.isEmpty()) {
        ui->statusLabel->setText(QStringLiteral("请输入要搜索的地址"));
        return;
    }
    if (m_searching)
        return;
    m_searching = true;
    ui->searchButton->setEnabled(false);
    ui->searchButton->setText(QStringLiteral("腾讯地图查询中…"));
    emit searchRequested(address);
}

void LocationDialog::onAccepted()
{
    accept();
}
