#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class LoginPage;
}
QT_END_NAMESPACE

class LoginPage : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPage(QWidget *parent = nullptr);
    ~LoginPage();

    QString phone() const;
    QString apiBaseUrl() const;
    bool demoMode() const;
    void setBusy(bool busy);
    void setStatus(const QString &text);

signals:
    void loginClicked();

private:
    Ui::LoginPage *ui;
};

#endif
