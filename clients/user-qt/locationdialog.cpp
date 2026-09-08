// 实现行政区域/示例位置选择，以及通过地图服务解析用户输入地址。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "locationdialog.h"
#include "ui_locationdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVariant>

/// 创建位置选择对话框并加载区域快捷项。
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
    ui->descLabel->setText(QStringLiteral("选择北京市区域，或输入详细地址定位附近充电站。"));
    ui->verticalLayout->setContentsMargins(16, 16, 16, 16);
    if (parent)
        resize(qMin(370, parent->width() - 24), qMin(520, parent->height() - 48));

    fillRegions();

    connect(ui->useRegionButton, &QPushButton::clicked, this, &LocationDialog::onUseRegion);
    connect(ui->searchButton, &QPushButton::clicked, this, &LocationDialog::onSearch);
    connect(ui->addressEdit, &QLineEdit::returnPressed, this, &LocationDialog::onSearch);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &LocationDialog::onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &LocationDialog::reject);
}

/// 接收地图服务返回的地址坐标并更新对话框。
void LocationDialog::applyGeocodedLocation(double lat, double lng, const QString &displayName)
{
    m_searching = false;
    ui->searchButton->setEnabled(true);
    ui->searchButton->setText(QStringLiteral("搜索位置"));
    applyLocation(lat, lng, displayName);
}

/// 显示地址搜索失败信息并恢复搜索按钮。
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

/// 初始化位置选择对话框的当前坐标和名称。
void LocationDialog::setCurrentLocation(double lat, double lng, const QString &displayName)
{
    m_lat = lat;
    m_lng = lng;
    m_displayName = displayName;
    ui->statusLabel->setText(QStringLiteral("当前：%1").arg(displayName));
}

/// 向区域下拉框加入北京市中心和常用区域快捷项。
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

/// 保存位置坐标和显示名称。
void LocationDialog::applyLocation(double lat, double lng, const QString &name)
{
    m_lat = lat;
    m_lng = lng;
    m_displayName = name;
    ui->statusLabel->setText(QStringLiteral("已选择：%1").arg(name));
}

/// 应用当前下拉区域。
void LocationDialog::onUseRegion()
{
    const QVariantMap data = ui->regionCombo->currentData().toMap();
    applyLocation(data.value(QStringLiteral("lat")).toDouble(),
                  data.value(QStringLiteral("lng")).toDouble(),
                  data.value(QStringLiteral("display")).toString());
}

/// 校验地址并发出地图搜索请求。
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

/// 确认并关闭位置选择对话框。
void LocationDialog::onAccepted()
{
    accept();
}
