#include "stationdetaildialog.h"
#include "ui_stationdetaildialog.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QIcon navigationArrowIcon()
{
    QPixmap pm(36, 36);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(18, 18);
    p.rotate(-28);
    QPainterPath path;
    path.moveTo(0, -14);
    path.lineTo(11, 13);
    path.lineTo(0, 6);
    path.lineTo(-11, 13);
    path.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#0d9488")));
    p.drawPath(path);
    return QIcon(pm);
}

} // namespace

StationDetailDialog::StationDetailDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::StationDetailDialog)
{
    ui->setupUi(this);
    resize(360, 680);
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->nameLabel->setObjectName(QStringLiteral("cardTitle"));
    ui->infoLabel->setObjectName(QStringLiteral("cardInfo"));
    ui->pilesTitleLabel->setObjectName(QStringLiteral("sectionLabel"));
    ui->pilesHintLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->navigateButton->setObjectName(QStringLiteral("navInlineButton"));
    ui->navigateButton->setIcon(navigationArrowIcon());
    ui->navigateButton->setIconSize(QSize(18, 18));
    ui->navigateButton->setCursor(Qt::PointingHandCursor);
    ui->navigateButton->setFlat(true);

    ui->pileStatusCombo->clear();
    ui->pileStatusCombo->addItem(QStringLiteral("全部状态"), QString());
    ui->pileStatusCombo->addItem(QStringLiteral("空闲"), QStringLiteral("IDLE"));
    ui->pileStatusCombo->addItem(QStringLiteral("已预约"), QStringLiteral("RESERVED"));
    ui->pileStatusCombo->addItem(QStringLiteral("充电中"), QStringLiteral("CHARGING"));
    ui->pileStatusCombo->addItem(QStringLiteral("故障"), QStringLiteral("FAULT"));
    ui->pileStatusCombo->addItem(QStringLiteral("离线"), QStringLiteral("OFFLINE"));
    ui->pileTypeCombo->clear();
    ui->pileTypeCombo->addItem(QStringLiteral("全部类型"), QString());
    ui->pileTypeCombo->addItem(QStringLiteral("快充"), QStringLiteral("FAST"));
    ui->pileTypeCombo->addItem(QStringLiteral("慢充"), QStringLiteral("SLOW"));

    connect(ui->navigateButton, &QPushButton::clicked, this, &StationDetailDialog::navigateClicked);
    connect(ui->pileStatusCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &StationDetailDialog::applyPileFilter);
    connect(ui->pileTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &StationDetailDialog::applyPileFilter);
}

StationDetailDialog::~StationDetailDialog()
{
    delete ui;
}

void StationDetailDialog::setStation(const StationSummary &station)
{
    const bool sameStation = (m_station.id == station.id && station.id != 0);
    const double previousDistance = m_station.distanceKm;
    m_station = station;
    if (sameStation && m_station.distanceKm < 0)
        m_station.distanceKm = previousDistance;
    ui->nameLabel->setText(station.name);

    const QString status = (m_station.status == QLatin1String("ACTIVE"))
                               ? QStringLiteral("营业中")
                               : (m_station.status.isEmpty() ? QStringLiteral("未知") : m_station.status);
    const QString dist = m_station.distanceKm < 0
                             ? QStringLiteral("未知")
                             : QStringLiteral("%1 km").arg(m_station.distanceKm, 0, 'f', 2);
    const QString price = m_station.priceCentsPerKwh > 0
                              ? centsToYuanText(m_station.priceCentsPerKwh) + QStringLiteral(" 元/度")
                              : QStringLiteral("未配置");
    QString extra;
    if (!m_station.district.isEmpty() || !m_station.operatorName.isEmpty()) {
        extra = QStringLiteral("\n区域：%1    运营商：%2")
                    .arg(m_station.district.isEmpty() ? QStringLiteral("未知") : m_station.district,
                         m_station.operatorName.isEmpty() ? QStringLiteral("未知") : m_station.operatorName);
    }
    if (!m_station.regionScope.isEmpty() || !m_station.locationType.isEmpty()) {
        extra += QStringLiteral("\n范围：%1    场所：%2")
                     .arg(m_station.regionScope.isEmpty() ? QStringLiteral("未知") : m_station.regionScope,
                          m_station.locationType.isEmpty() ? QStringLiteral("未知") : m_station.locationType);
    }

    ui->infoLabel->setText(
        QStringLiteral("地址：%1\n距离：%2\n电价：%3\n空闲桩：%4 / %5\n状态：%6\n在线率：%7%%8")
            .arg(m_station.address)
            .arg(dist)
            .arg(price)
            .arg(m_station.availablePiles)
            .arg(m_station.totalPiles)
            .arg(status)
            .arg(m_station.onlineRate, 0, 'f', 1)
            .arg(extra));
    ui->navigateButton->setEnabled(m_station.hasCoordinates);
    ui->navigateButton->setToolTip(m_station.hasCoordinates
                                       ? QString()
                                       : QStringLiteral("该站点暂无坐标，无法导航"));
    if (!sameStation) {
        ui->pilesHintLabel->setText(QStringLiteral("正在加载电桩…"));
        m_allPiles.clear();
        clearPileCards();
    }
}

QString StationDetailDialog::selectedPileStatus() const
{
    return ui->pileStatusCombo->currentData().toString();
}

QString StationDetailDialog::selectedPileType() const
{
    return ui->pileTypeCombo->currentData().toString();
}

void StationDetailDialog::showPilesHint(const QString &text)
{
    ui->pilesHintLabel->setText(text);
}

void StationDetailDialog::clearPileCards()
{
    while (ui->pilesLayout->count() > 1) {
        QLayoutItem *item = ui->pilesLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

void StationDetailDialog::showPiles(qint64 stationId, const QVector<ChargingPile> &piles)
{
    if (stationId != m_station.id)
        return;
    m_allPiles = piles;
    applyPileFilter();
}

void StationDetailDialog::applyPileFilter()
{
    QVector<ChargingPile> filtered;
    const QString status = selectedPileStatus();
    const QString type = selectedPileType();
    for (const ChargingPile &p : m_allPiles) {
        if (!status.isEmpty() && p.status != status)
            continue;
        if (!type.isEmpty() && p.pileType != type)
            continue;
        filtered.push_back(p);
    }
    renderPileCards(filtered);
}

void StationDetailDialog::renderPileCards(const QVector<ChargingPile> &piles)
{
    clearPileCards();
    if (m_allPiles.isEmpty()) {
        ui->pilesHintLabel->setText(
            QStringLiteral("该站暂无系统接入的电桩。公共目录站点可能只有接口数量，没有实际电桩记录。"));
        return;
    }
    if (piles.isEmpty()) {
        ui->pilesHintLabel->setText(QStringLiteral("没有符合筛选条件的电桩。"));
        return;
    }
    ui->pilesHintLabel->setText(QStringLiteral("显示 %1 / %2 个电桩。空闲桩可直接预约。")
                                    .arg(piles.size())
                                    .arg(m_allPiles.size()));

    const bool stationBookable = m_station.isBookable || m_station.priceCentsPerKwh > 0;
    for (const ChargingPile &p : piles) {
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("pileCard"));
        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(12, 10, 12, 10);
        box->setSpacing(6);

        auto *titleRow = new QHBoxLayout;
        auto *no = new QLabel(p.pileNo);
        no->setObjectName(QStringLiteral("pileNo"));
        auto *st = new QLabel(pileStatusText(p.status));
        st->setObjectName(QStringLiteral("pileStatus_") + p.status);
        titleRow->addWidget(no);
        titleRow->addStretch();
        titleRow->addWidget(st);

        auto *meta = new QLabel(QStringLiteral("类型：%1    功率：%2\n累计充电 %3 次")
                                    .arg(pileTypeText(p.pileType), powerText(p.ratedPowerW))
                                    .arg(p.totalChargeCount));
        meta->setObjectName(QStringLiteral("cardInfo"));
        meta->setWordWrap(true);

        auto *btn = new QPushButton;
        btn->setCursor(Qt::PointingHandCursor);
        if (p.status == QLatin1String("IDLE") && stationBookable && m_station.priceCentsPerKwh > 0) {
            btn->setText(QStringLiteral("预约充电"));
            connect(btn, &QPushButton::clicked, this, [this, p]() {
                emit reserveClicked(p);
            });
        } else {
            btn->setObjectName(QStringLiteral("secondaryButton"));
            btn->setText(QStringLiteral("查看电桩"));
            connect(btn, &QPushButton::clicked, this, [this, p]() {
                emit pileClicked(p);
            });
        }

        box->addLayout(titleRow);
        box->addWidget(meta);
        box->addWidget(btn);
        ui->pilesLayout->insertWidget(ui->pilesLayout->count() - 1, card);
    }
}
