#include "homepage.h"
#include "ui_homepage.h"

#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyle>
#include <QtGlobal>
#include <QToolButton>
#include <QVariant>

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HomePage)
{
    ui->setupUi(this);
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->distanceDesc->setObjectName(QStringLiteral("subtitleLabel"));
    ui->keywordDesc->setObjectName(QStringLiteral("subtitleLabel"));
    ui->hintLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->radiusHint->setObjectName(QStringLiteral("subtitleLabel"));
    ui->locationButton->setObjectName(QStringLiteral("locationButton"));
    ui->locationButton->setStyleSheet(QStringLiteral("text-align: left; padding: 12px;"));
    ui->distanceModeButton->setObjectName(QStringLiteral("modeTab"));
    ui->keywordModeButton->setObjectName(QStringLiteral("modeTab"));
    ui->radiusUp->setObjectName(QStringLiteral("stepButton"));
    ui->radiusDown->setObjectName(QStringLiteral("stepButton"));

    setupFilterCombos();
    setupRadiusControl();
    setupModeSwitch();
    refreshLocationButton();
    setKeywordMode(false);

    connect(ui->locationButton, &QPushButton::clicked, this, &HomePage::changeLocationClicked);
    connect(ui->queryButton, &QPushButton::clicked, this, &HomePage::queryClicked);
    connect(ui->searchButton, &QPushButton::clicked, this, &HomePage::searchClicked);
    connect(ui->keywordEdit, &QLineEdit::returnPressed, this, &HomePage::searchClicked);
    connect(ui->activeOrderButton, &QPushButton::clicked, this, &HomePage::activeOrderClicked);
    ui->activeOrderButton->setObjectName(QStringLiteral("locationButton"));
    ui->activeOrderButton->setVisible(false);
}

HomePage::~HomePage()
{
    delete ui;
}

double HomePage::radiusKm() const
{
    return m_radiusKm;
}

QString HomePage::keyword() const
{
    return ui->keywordEdit->text().trimmed();
}

QString HomePage::selectedDistrict() const
{
    return ui->districtCombo->currentData().toString();
}

QString HomePage::selectedOperator() const
{
    return ui->operatorCombo->currentData().toString();
}

QString HomePage::selectedRegionScope() const
{
    return ui->regionCombo->currentData().toString();
}

QString HomePage::selectedLocationType() const
{
    return ui->locationCombo->currentData().toString();
}

bool HomePage::availableOnly() const
{
    return ui->availableOnlyCheck->isChecked();
}

bool HomePage::bookableOnly() const
{
    return ui->bookableOnlyCheck->isChecked();
}

void HomePage::setupFilterCombos()
{
    ui->districtCombo->setMaxVisibleItems(12);
    ui->operatorCombo->setMaxVisibleItems(12);
    ui->regionCombo->setMaxVisibleItems(12);
    ui->locationCombo->setMaxVisibleItems(12);
    fillFilterCombo(ui->districtCombo, QStringLiteral("全部行政区"), {});
    fillFilterCombo(ui->operatorCombo, QStringLiteral("全部运营商"), {});
    fillFilterCombo(ui->regionCombo, QStringLiteral("全部范围"), {});
    fillFilterCombo(ui->locationCombo, QStringLiteral("全部场所"), {});
}

void HomePage::fillFilterCombo(QComboBox *box, const QString &allLabel, const QStringList &values)
{
    const QString current = box->currentData().toString();
    const QSignalBlocker blocker(box);
    box->clear();
    box->addItem(allLabel, QString());
    for (const QString &value : values) {
        if (!value.trimmed().isEmpty())
            box->addItem(value, value);
    }
    const int idx = box->findData(current);
    box->setCurrentIndex(idx >= 0 ? idx : 0);
}

void HomePage::setFilterOptions(const StationFilterOptions &options)
{
    fillFilterCombo(ui->districtCombo, QStringLiteral("全部行政区"), options.districts);
    fillFilterCombo(ui->operatorCombo, QStringLiteral("全部运营商"), options.operatorNames);
    fillFilterCombo(ui->regionCombo, QStringLiteral("全部范围"), options.regionScopes);
    fillFilterCombo(ui->locationCombo, QStringLiteral("全部场所"), options.locationTypes);
}

void HomePage::setupModeSwitch()
{
    ui->distanceModeButton->setCheckable(true);
    ui->keywordModeButton->setCheckable(true);
    connect(ui->distanceModeButton, &QPushButton::clicked, this, [this]() {
        setKeywordMode(false);
    });
    connect(ui->keywordModeButton, &QPushButton::clicked, this, [this]() {
        setKeywordMode(true);
    });
}

void HomePage::setKeywordMode(bool keywordMode)
{
    const bool changed = (m_keywordMode != keywordMode);
    m_keywordMode = keywordMode;
    ui->distanceModeButton->setChecked(!keywordMode);
    ui->keywordModeButton->setChecked(keywordMode);
    ui->distancePage->setVisible(!keywordMode);
    ui->keywordPage->setVisible(keywordMode);
    ui->distanceModeButton->style()->unpolish(ui->distanceModeButton);
    ui->distanceModeButton->style()->polish(ui->distanceModeButton);
    ui->keywordModeButton->style()->unpolish(ui->keywordModeButton);
    ui->keywordModeButton->style()->polish(ui->keywordModeButton);

    clearCards();
    m_stations.clear();
    if (keywordMode)
        showHint(QStringLiteral("输入关键词或选择筛选条件后点击「搜索」。"));
    else
        showHint(QStringLiteral("选择范围后点击「查询附近站点」。"));
    if (!keywordMode && changed)
        emit queryClicked();
}

void HomePage::resetToDistanceMode()
{
    setKeywordMode(false);
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

void HomePage::setActiveOrder(bool hasOrder, const ChargingOrder &order)
{
    ui->activeOrderButton->setVisible(hasOrder);
    if (!hasOrder)
        return;
    ui->activeOrderButton->setText(
        QStringLiteral("当前订单  %1  ·  %2  ›")
            .arg(orderStatusText(order.status),
                 order.stationName.isEmpty() ? order.orderNo : order.stationName));
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

bool HomePage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            bool ok = false;
            const int index = watched->property("stationIndex").toInt(&ok);
            if (ok && index >= 0 && index < m_stations.size()) {
                emit stationClicked(m_stations.at(index));
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void HomePage::showStations(const QVector<StationSummary> &stations)
{
    if (m_keywordMode)
        return;
    renderStations(stations,
                   QStringLiteral("附近没有充电站，可加大范围或换一个位置。"),
                   QStringLiteral("共 %1 座充电站，已按距离由近到远排序。点击站点查看详情。")
                       .arg(stations.size()));
}

void HomePage::showSearchStations(const QVector<StationSummary> &stations, int total)
{
    if (!m_keywordMode)
        return;
    QVector<StationSummary> ranked = stations;
    for (StationSummary &s : ranked) {
        if (s.hasCoordinates) {
            s.distanceKm = geoDistanceKm(m_lat, m_lng, s.latitude, s.longitude);
        } else {
            s.distanceKm = -1;
        }
    }
    std::sort(ranked.begin(), ranked.end(), [](const StationSummary &a, const StationSummary &b) {
        const bool aOk = a.distanceKm >= 0;
        const bool bOk = b.distanceKm >= 0;
        if (aOk != bOk)
            return aOk;
        if (aOk)
            return a.distanceKm < b.distanceKm;
        return a.id < b.id;
    });

    const int shown = ranked.size();
    const int all = qMax(total, shown);
    QString okHint;
    if (all > shown) {
        okHint = QStringLiteral("共 %1 座站点，当前显示前 %2 座；有坐标的已按距当前位置由近到远排序。")
                     .arg(all)
                     .arg(shown);
    } else {
        okHint = QStringLiteral("共 %1 座站点，有坐标的已按距当前位置由近到远排序。点击查看详情。")
                     .arg(shown);
    }
    renderStations(ranked,
                   QStringLiteral("没有符合条件的站点，可换个关键词或筛选条件。"),
                   okHint);
}

void HomePage::renderStations(const QVector<StationSummary> &stations,
                             const QString &emptyHint,
                             const QString &okHint)
{
    m_stations = stations;
    clearCards();
    if (stations.isEmpty()) {
        showHint(emptyHint);
        return;
    }
    showHint(okHint);

    for (int i = 0; i < stations.size(); ++i) {
        const StationSummary &s = stations.at(i);
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("card"));
        card->setCursor(Qt::PointingHandCursor);
        card->setProperty("stationIndex", i);
        card->installEventFilter(this);

        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(14, 12, 14, 12);

        auto *titleRow = new QHBoxLayout;
        auto *name = new QLabel(s.name);
        name->setObjectName(QStringLiteral("cardTitle"));
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *chevron = new QLabel(QStringLiteral("›"));
        chevron->setObjectName(QStringLiteral("subtitleLabel"));
        chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(name);
        titleRow->addStretch();
        titleRow->addWidget(chevron);

        QStringList meta;
        if (!s.district.isEmpty())
            meta << s.district;
        if (!s.operatorName.isEmpty())
            meta << s.operatorName;
        const QString price = s.priceCentsPerKwh > 0
                                  ? centsToYuanText(s.priceCentsPerKwh) + QStringLiteral(" 元/度")
                                  : QStringLiteral("电价未配置");
        QString stats = QStringLiteral("%1  ·  空闲 %2 / %3")
                            .arg(price)
                            .arg(s.availablePiles)
                            .arg(s.totalPiles);
        if (s.distanceKm >= 0)
            stats += QStringLiteral("  ·  %1 km").arg(s.distanceKm, 0, 'f', 2);
        else if (!s.hasCoordinates)
            stats += QStringLiteral("  ·  无坐标");

        QString infoText = stats + QLatin1Char('\n') + s.address;
        if (!meta.isEmpty())
            infoText = meta.join(QStringLiteral("  ·  ")) + QLatin1Char('\n') + infoText;

        auto *info = new QLabel(infoText);
        info->setObjectName(QStringLiteral("cardInfo"));
        info->setWordWrap(true);
        info->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto *clickHint = new QLabel(QStringLiteral("点击查看电桩并预约"));
        clickHint->setObjectName(QStringLiteral("subtitleLabel"));
        clickHint->setAttribute(Qt::WA_TransparentForMouseEvents);

        box->addLayout(titleRow);
        box->addWidget(info);
        box->addWidget(clickHint);
        ui->stationListLayout->insertWidget(ui->stationListLayout->count() - 1, card);
    }
}
