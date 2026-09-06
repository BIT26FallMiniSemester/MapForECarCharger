#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "models.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class ApiClient;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onLoginClicked();
    void onLoginSucceeded(const QString &token, const User &user, bool isNewUser);
    void onQueryNearby();
    void onChangeLocation();
    void onSaveNickname();
    void onChooseAvatar();
    void onRecharge();
    void onLogout();
    void onApiFailed(int code, const QString &message);
    void setBusy(bool busy);

private:
    void applyTheme();
    void showAppPage();
    void applyUser(const User &user);
    QString avatarSettingKey() const;

    Ui::MainWindow *ui;
    ApiClient *m_api = nullptr;
    User m_user;
    int m_inflight = 0;
};

#endif
