#ifndef PROFILEPAGE_H
#define PROFILEPAGE_H

#include "models.h"

#include <QVector>
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

    QString nickname() const;
    double rechargeYuan() const;
    void setUser(const User &user);
    void setAvatarPath(const QString &path);
    void setBusy(bool busy);
    void showRechargeRecords(const QVector<RechargeRecord> &records);
    void setStatus(const QString &text);

signals:
    void saveNicknameClicked();
    void chooseAvatarClicked();
    void rechargeClicked();
    void logoutClicked();

private:
    Ui::ProfilePage *ui;
};

#endif
