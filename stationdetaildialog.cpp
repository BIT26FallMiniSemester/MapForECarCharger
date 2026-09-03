#include "stationdetaildialog.h"
#include "ui_stationdetaildialog.h"

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

    connect(ui->navigateButton, &QPushButton::clicked, this, &StationDetailDialog::navigateClicked);
}

StationDetailDialog::~StationDetailDialog()
{
    delete ui;
}

void StationDetailDialog::setStation(const StationSummary &station)
{
    m_station = station;
    ui->nameLabel->setText(station.name);

    const QString status = (station.status == QLatin1String("ACTIVE"))
                               ? QStringLiteral("营业中")
                               : (station.status.isEmpty() ? QStringLiteral("未知") : station.status);
    const QString dist = station.distanceKm < 0
                             ? QStringLiteral("未知")
                             : QStringLiteral("%1 km").arg(station.distanceKm, 0, 'f', 2);

    ui->infoLabel->setText(
        QStringLiteral("地址：%1\n距离：%2\n电价：%3 元/度\n空闲桩：%4 / %5\n状态：%6\n在线率：%7%")
            .arg(station.address)
            .arg(dist)
            .arg(centsToYuanText(station.priceCentsPerKwh))
            .arg(station.availablePiles)
            .arg(station.totalPiles)
            .arg(status)
            .arg(station.onlineRate, 0, 'f', 1));
    ui->pilesHintLabel->setText(QStringLiteral("正在加载电桩…"));
    clearPileCards();
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

    clearPileCards();
    if (piles.isEmpty()) {
        ui->pilesHintLabel->setText(
            QStringLiteral("该站暂无系统接入的电桩。公共目录站点可能只有接口数量，没有实际电桩记录。"));
        return;
    }
    ui->pilesHintLabel->setText(QStringLiteral("共 %1 个电桩，按编号排序").arg(piles.size()));

    for (const ChargingPile &p : piles) {
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("pileCard"));
        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(12, 10, 12, 10);
        box->setSpacing(4);

        auto *titleRow = new QHBoxLayout;
        auto *no = new QLabel(p.pileNo);
        no->setObjectName(QStringLiteral("pileNo"));
        auto *st = new QLabel(pileStatusText(p.status));
        st->setObjectName(QStringLiteral("pileStatus_") + p.status);
        titleRow->addWidget(no);
        titleRow->addStretch();
        titleRow->addWidget(st);

        auto *meta = new QLabel(QStringLiteral("类型：%1    功率：%2")
                                    .arg(pileTypeText(p.pileType), powerText(p.ratedPowerW)));
        meta->setObjectName(QStringLiteral("cardInfo"));
        meta->setWordWrap(true);

        box->addLayout(titleRow);
        box->addWidget(meta);
        ui->pilesLayout->insertWidget(ui->pilesLayout->count() - 1, card);
    }
}
