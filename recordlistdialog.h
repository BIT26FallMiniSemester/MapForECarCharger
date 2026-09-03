#ifndef RECORDLISTDIALOG_H
#define RECORDLISTDIALOG_H

#include "models.h"

#include <QDialog>
#include <QVector>

QT_BEGIN_NAMESPACE
namespace Ui {
class RecordListDialog;
}
QT_END_NAMESPACE

class RecordListDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RecordListDialog(QWidget *parent = nullptr);
    ~RecordListDialog();

    void setHeading(const QString &title);
    void showHint(const QString &text);
    void showRechargeRecords(const QVector<RechargeRecord> &records);
    void showOrders(const QVector<ChargingOrder> &orders);

private:
    void clearItems();
    void addLine(const QString &title, const QString &detail);

    Ui::RecordListDialog *ui;
};

#endif
