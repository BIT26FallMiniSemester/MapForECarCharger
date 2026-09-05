#include "recordlistdialog.h"
#include "ui_recordlistdialog.h"

#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

RecordListDialog::RecordListDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RecordListDialog)
{
    ui->setupUi(this);
    resize(360, 560);
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->hintLabel->setObjectName(QStringLiteral("subtitleLabel"));
}

RecordListDialog::~RecordListDialog()
{
    delete ui;
}

void RecordListDialog::setHeading(const QString &title)
{
    setWindowTitle(title);
    ui->titleLabel->setText(title);
}

void RecordListDialog::showHint(const QString &text)
{
    ui->hintLabel->setText(text);
}

void RecordListDialog::clearItems()
{
    while (ui->listLayout->count() > 1) {
        QLayoutItem *item = ui->listLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

bool RecordListDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            bool ok = false;
            const int index = watched->property("orderIndex").toInt(&ok);
            if (ok && index >= 0 && index < m_orders.size()) {
                emit orderClicked(m_orders.at(index));
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void RecordListDialog::addLine(const QString &title, const QString &detail, int orderIndex)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("pileCard"));
    if (orderIndex >= 0) {
        card->setCursor(Qt::PointingHandCursor);
        card->setProperty("orderIndex", orderIndex);
        card->installEventFilter(this);
    }
    auto *box = new QVBoxLayout(card);
    box->setContentsMargins(12, 10, 12, 10);
    auto *t = new QLabel(title);
    t->setObjectName(QStringLiteral("pileNo"));
    t->setWordWrap(true);
    t->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *d = new QLabel(detail);
    d->setObjectName(QStringLiteral("cardInfo"));
    d->setWordWrap(true);
    d->setAttribute(Qt::WA_TransparentForMouseEvents);
    box->addWidget(t);
    box->addWidget(d);
    ui->listLayout->insertWidget(ui->listLayout->count() - 1, card);
}

void RecordListDialog::showRechargeRecords(const QVector<RechargeRecord> &records)
{
    m_orders.clear();
    clearItems();
    if (records.isEmpty()) {
        ui->hintLabel->setText(QStringLiteral("暂无充值记录"));
        return;
    }
    ui->hintLabel->setText(QStringLiteral("共 %1 条充值记录").arg(records.size()));
    for (const RechargeRecord &r : records) {
        addLine(QStringLiteral("+¥%1    余额 ¥%2")
                    .arg(centsToYuanText(r.amountCents), centsToYuanText(r.balanceAfterCents)),
                QStringLiteral("%1\n%2").arg(r.rechargeNo, formatIsoUtc(r.createdAt)));
    }
}

void RecordListDialog::showOrders(const QVector<ChargingOrder> &orders)
{
    m_orders = orders;
    clearItems();
    if (orders.isEmpty()) {
        ui->hintLabel->setText(QStringLiteral("暂无订单记录"));
        return;
    }
    ui->hintLabel->setText(QStringLiteral("共 %1 条订单，点击可查看详情或继续操作").arg(orders.size()));
    for (int i = 0; i < orders.size(); ++i) {
        const ChargingOrder &o = orders.at(i);
        QString extra = QStringLiteral("%1  ·  %2\n金额 ¥%3")
                            .arg(o.stationName, o.pileNo, centsToYuanText(o.amountCents));
        if (o.energyWh > 0)
            extra += QStringLiteral("  ·  %1").arg(energyText(o.energyWh));
        extra += QLatin1Char('\n') + formatIsoUtc(o.createdAt);
        addLine(QStringLiteral("%1    %2").arg(o.orderNo, orderStatusText(o.status)), extra, i);
    }
}
