#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>
namespace Ui { class LoginWindow; }
class ApiClient;
class QCheckBox;
class QLineEdit;
class LoginWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWindow(QWidget *parent = nullptr);
    ~LoginWindow();
private slots:
    void login();
private:
    Ui::LoginWindow *ui;
    ApiClient *m_api;
    QCheckBox *m_demoCheck;
    QLineEdit *m_serverEdit;
    void openMainWindow(bool demoMode, const QString &token = QString());
};
#endif
