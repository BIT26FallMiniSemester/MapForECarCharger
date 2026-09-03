#ifndef RECHARGEDIALOG_H
#define RECHARGEDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class RechargeDialog;
}
QT_END_NAMESPACE

class QButtonGroup;

class RechargeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RechargeDialog(QWidget *parent = nullptr);
    ~RechargeDialog();

    double amountYuan() const;

private:
    void syncPresetButtons(double yuan);

    Ui::RechargeDialog *ui;
    QButtonGroup *m_presets = nullptr;
};

#endif
