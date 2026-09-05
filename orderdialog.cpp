#include "orderdialog.h"
#include "ui_orderdialog.h"

#include <QDateTime>
#include <QPushButton>
#include <QStyle>
#include <QTimer>

OrderDialog::OrderDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OrderDialog)
    , m_tick(new QTimer(this))
{
    ui->setupUi(this);
    resize(360, 680);
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->statusBadge->setObjectName(QStringLiteral("cardTitle"));
    ui->infoLabel->setObjectName(QStringLiteral("cardInfo"));
    ui->liveLabel->setObjectName(QStringLiteral("sectionLabel"));
    ui->hintLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->secondaryButton->setObjectName(QStringLiteral("secondaryButton"));
    ui->rechargeButton->setObjectName(QStringLiteral("secondaryButton"));
    ui->expireButton->setObjectName(QStringLiteral("secondaryButton"));
    ui->closeButton->setObjectName(QStringLiteral("secondaryButton"));

    connect(ui->primaryButton, &QPushButton::clicked, this, &OrderDialog::onPrimaryClicked);
    connect(ui->secondaryButton, &QPushButton::clicked, this, &OrderDialog::onSecondaryClicked);
    connect(ui->rechargeButton, &QPushButton::clicked, this, &OrderDialog::rechargeRequested);
    connect(ui->expireButton, &QPushButton::clicked, this, &OrderDialog::expireDemoRequested);
    connect(ui->closeButton, &QPushButton::clicked, this, &OrderDialog::reject);
    connect(m_tick, &QTimer::timeout, this, &OrderDialog::onTick);
    m_tick->start(1000);
}

OrderDialog::~OrderDialog()
{
    delete ui;
}

void OrderDialog::setOrder(const ChargingOrder &order)
{
    if (order.id == 0)
        return;
    m_order = order;
    refreshUi();
}

void OrderDialog::setBalanceCents(qint64 cents)
{
    m_balanceCents = cents;
    refreshUi();
}

void OrderDialog::setDemoMode(bool enabled)
{
    m_demoMode = enabled;
    refreshUi();
}

void OrderDialog::setHint(const QString &text, bool success)
{
    ui->hintLabel->setText(text);
    ui->hintLabel->setObjectName(success ? QStringLiteral("successLabel")
                                         : QStringLiteral("statusLabel"));
    ui->hintLabel->style()->unpolish(ui->hintLabel);
    ui->hintLabel->style()->polish(ui->hintLabel);
    ui->hintLabel->update();
}

void OrderDialog::onPrimaryClicked()
{
    if (m_order.status == QLatin1String("PENDING"))
        emit reserveRequested(m_order.id);
    else if (m_order.status == QLatin1String("RESERVED"))
        emit startRequested(m_order.id);
    else if (m_order.status == QLatin1String("CHARGING"))
        emit stopRequested(m_order.id);
    else if (m_order.status == QLatin1String("UNPAID"))
        emit settleRequested(m_order.id);
}

void OrderDialog::onSecondaryClicked()
{
    const QString reason = (m_order.status == QLatin1String("RESERVED"))
                               ? QStringLiteral("用户取消预约")
                               : QStringLiteral("用户取消订单");
    emit cancelRequested(m_order.id, reason);
}

void OrderDialog::onTick()
{
    if (m_order.status == QLatin1String("RESERVED") || m_order.status == QLatin1String("CHARGING"))
        refreshUi();
    if (m_order.status == QLatin1String("CHARGING") && m_order.id > 0 && !m_demoMode) {
        if (--m_pollCountdown <= 0) {
            m_pollCountdown = 5;
            emit refreshRequested(m_order.id);
        }
    } else {
        m_pollCountdown = 5;
    }
}

int OrderDialog::liveDurationSeconds() const
{
    if (m_order.status == QLatin1String("CHARGING")) {
        QDateTime started = QDateTime::fromString(m_order.startedAt, Qt::ISODate);
        if (!started.isValid())
            started = QDateTime::fromString(m_order.startedAt, Qt::ISODateWithMs);
        if (!started.isValid())
            return m_order.durationSeconds;
        const int seconds = static_cast<int>(started.secsTo(QDateTime::currentDateTimeUtc()));
        return qMax(0, seconds);
    }
    return m_order.durationSeconds;
}

qint64 OrderDialog::liveEnergyWh() const
{
    if (m_order.status == QLatin1String("CHARGING") && m_order.energyWh <= 0)
        return estimateEnergyWh(m_order.ratedPowerW, liveDurationSeconds());
    return m_order.energyWh;
}

void OrderDialog::refreshUi()
{
    ui->statusBadge->setText(orderStatusText(m_order.status));

    const qint64 energy = liveEnergyWh();
    const int duration = liveDurationSeconds();
    const qint64 amount = (m_order.status == QLatin1String("CHARGING") && m_order.amountCents <= 0)
                              ? estimateAmountCents(energy, m_order.unitPriceCentsPerKwh)
                              : m_order.amountCents;

    QStringList lines;
    lines << QStringLiteral("订单号：%1").arg(m_order.orderNo.isEmpty() ? QStringLiteral("—") : m_order.orderNo);
    lines << QStringLiteral("充电站：%1").arg(m_order.stationName);
    lines << QStringLiteral("电桩：%1").arg(m_order.pileNo);
    lines << QStringLiteral("电价：%1 元/度").arg(m_order.unitPriceCentsPerKwh > 0
                                                    ? centsToYuanText(m_order.unitPriceCentsPerKwh)
                                                    : QStringLiteral("—"));
    lines << QStringLiteral("电量：%1").arg(energy > 0 ? energyText(energy) : QStringLiteral("—"));
    lines << QStringLiteral("时长：%1").arg(duration > 0 ? formatDurationSeconds(duration) : QStringLiteral("—"));
    lines << QStringLiteral("金额：¥ %1").arg(centsToYuanText(amount));
    lines << QStringLiteral("钱包余额：¥ %1").arg(centsToYuanText(m_balanceCents));
    lines << QStringLiteral("创建时间：%1").arg(formatIsoUtc(m_order.createdAt));
    if (!m_order.reservedAt.isEmpty())
        lines << QStringLiteral("预约时间：%1").arg(formatIsoUtc(m_order.reservedAt));
    if (!m_order.reservationExpiresAt.isEmpty())
        lines << QStringLiteral("预约截止：%1").arg(formatIsoUtc(m_order.reservationExpiresAt));
    if (!m_order.startedAt.isEmpty())
        lines << QStringLiteral("开始充电：%1").arg(formatIsoUtc(m_order.startedAt));
    if (!m_order.stoppedAt.isEmpty())
        lines << QStringLiteral("结束充电：%1").arg(formatIsoUtc(m_order.stoppedAt));
    if (!m_order.settledAt.isEmpty())
        lines << QStringLiteral("结算时间：%1").arg(formatIsoUtc(m_order.settledAt));
    if (!m_order.cancelledAt.isEmpty())
        lines << QStringLiteral("取消时间：%1").arg(formatIsoUtc(m_order.cancelledAt));
    if (!m_order.cancelReason.isEmpty())
        lines << QStringLiteral("取消原因：%1").arg(m_order.cancelReason);
    ui->infoLabel->setText(lines.join(QLatin1Char('\n')));

    QString live;
    if (m_order.status == QLatin1String("RESERVED")) {
        QDateTime expires = QDateTime::fromString(m_order.reservationExpiresAt, Qt::ISODate);
        if (!expires.isValid())
            expires = QDateTime::fromString(m_order.reservationExpiresAt, Qt::ISODateWithMs);
        if (expires.isValid()) {
            const int left = static_cast<int>(QDateTime::currentDateTimeUtc().secsTo(expires));
            if (left <= 0)
                live = QStringLiteral("预约已超时，开始充电将被拒绝并释放电桩。");
            else
                live = QStringLiteral("请在 %1 内到达并开始充电。").arg(formatDurationSeconds(left));
        } else {
            live = QStringLiteral("请尽快到达充电站并开始充电。");
        }
    } else if (m_order.status == QLatin1String("CHARGING")) {
        live = QStringLiteral("正在模拟充电… 已充 %1，预估 ¥%2")
                   .arg(energyText(energy), centsToYuanText(amount));
    } else if (m_order.status == QLatin1String("UNPAID")) {
        if (m_balanceCents < m_order.amountCents)
            live = QStringLiteral("余额不足，请先充值再结算。订单会保持待结算。");
        else
            live = QStringLiteral("充电已结束，确认后将从余额扣款。");
    } else if (m_order.status == QLatin1String("PENDING")) {
        live = QStringLiteral("订单已创建，确认预约后才会占用电桩。");
    } else if (m_order.status == QLatin1String("COMPLETED")) {
        live = QStringLiteral("订单已完成，扣款不会重复执行。");
    } else if (m_order.status == QLatin1String("CANCELLED")) {
        live = QStringLiteral("订单已取消。");
    }
    ui->liveLabel->setText(live);

    const bool pending = m_order.status == QLatin1String("PENDING");
    const bool reserved = m_order.status == QLatin1String("RESERVED");
    const bool charging = m_order.status == QLatin1String("CHARGING");
    const bool unpaid = m_order.status == QLatin1String("UNPAID");

    ui->primaryButton->setVisible(pending || reserved || charging || unpaid);
    if (pending)
        ui->primaryButton->setText(QStringLiteral("预约电桩"));
    else if (reserved)
        ui->primaryButton->setText(QStringLiteral("开始充电"));
    else if (charging)
        ui->primaryButton->setText(QStringLiteral("结束充电"));
    else if (unpaid)
        ui->primaryButton->setText(QStringLiteral("去结算"));

    ui->secondaryButton->setVisible(pending || reserved);
    ui->rechargeButton->setVisible(unpaid);
    ui->expireButton->setVisible(m_demoMode && reserved);
}
