#ifndef ORDERDIALOG_H
#define ORDERDIALOG_H

#include "models.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class OrderDialog;
}
QT_END_NAMESPACE

class QTimer;

class OrderDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OrderDialog(QWidget *parent = nullptr);
    ~OrderDialog();

    void setOrder(const ChargingOrder &order);
    void setBalanceCents(qint64 cents);
    void setDemoMode(bool enabled);
    void setHint(const QString &text, bool success = false);
    ChargingOrder order() const { return m_order; }

signals:
    void reserveRequested(qint64 orderId);
    void startRequested(qint64 orderId);
    void stopRequested(qint64 orderId);
    void settleRequested(qint64 orderId);
    void cancelRequested(qint64 orderId, const QString &reason);
    void rechargeRequested();
    void expireDemoRequested();
    void refreshRequested(qint64 orderId);

private slots:
    void onPrimaryClicked();
    void onSecondaryClicked();
    void onTick();

private:
    void refreshUi();
    int liveDurationSeconds() const;
    qint64 liveEnergyWh() const;

    Ui::OrderDialog *ui;
    ChargingOrder m_order;
    qint64 m_balanceCents = 0;
    bool m_demoMode = true;
    int m_pollCountdown = 5;
    QTimer *m_tick = nullptr;
};

#endif
