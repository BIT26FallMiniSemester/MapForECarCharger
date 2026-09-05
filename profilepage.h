#ifndef PROFILEPAGE_H
#define PROFILEPAGE_H

#include "models.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ProfilePage;
}
QT_END_NAMESPACE

class ProfilePage : public QWidget
{
    Q_OBJECT

public:
    explicit ProfilePage(QWidget *parent = nullptr);
    ~ProfilePage();

    void setUser(const User &user);
    void setAvatarPath(const QString &path);
    void setBusy(bool busy);
    void setStatus(const QString &text, bool success = false);
    void setActiveOrder(bool hasOrder, const ChargingOrder &order);

signals:
    void editClicked();
    void rechargeClicked();
    void rechargeRecordsClicked();
    void orderRecordsClicked();
    void activeOrderClicked();
    void logoutClicked();

private:
    Ui::ProfilePage *ui;
};

#endif
