#include "recordlistdialog.h"
#include "ui_recordlistdialog.h"

#include <QFrame>
#include <QLabel>
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

void RecordListDialog::addLine(const QString &title, const QString &detail)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("pileCard"));
    auto *box = new QVBoxLayout(card);
    box->setContentsMargins(12, 10, 12, 10);
    auto *t = new QLabel(title);
    t->setObjectName(QStringLiteral("pileNo"));
    t->setWordWrap(true);
    auto *d = new QLabel(detail);
    d->setObjectName(QStringLiteral("cardInfo"));
    d->setWordWrap(true);
    box->addWidget(t);
    box->addWidget(d);
    ui->listLayout->insertWidget(ui->listLayout->count() - 1, card);
}

void RecordListDialog::showRechargeRecords(const QVector<RechargeRecord> &records)
{
    clearItems();
    if (records.isEmpty()) {
        ui->hintLabel->setText(QStringLiteral("暂无充值记录"));
        return;
    }
    ui->hintLabel->setText(QStringLiteral("共 %1 条充值记录").arg(records.size()));
    for (const RechargeRecord &r : records) {
        addLine(QStringLiteral("+¥%1    余额 ¥%2")
                    .arg(centsToYuanText(r.amountCents), centsToYuanText(r.balanceAfterCents)),
                QStringLiteral("%1\n%2").arg(r.rechargeNo, r.createdAt));
    }
}

void RecordListDialog::showOrders(const QVector<ChargingOrder> &orders)
{
    clearItems();
    if (orders.isEmpty()) {
        ui->hintLabel->setText(QStringLiteral("暂无订单记录"));
        return;
    }
    ui->hintLabel->setText(QStringLiteral("共 %1 条订单").arg(orders.size()));
    for (const ChargingOrder &o : orders) {
        addLine(QStringLiteral("%1    %2").arg(o.orderNo, orderStatusText(o.status)),
                QStringLiteral("%1  ·  %2\n金额 ¥%3\n%4")
                    .arg(o.stationName, o.pileNo, centsToYuanText(o.amountCents), o.createdAt));
    }
}
