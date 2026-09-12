// 声明管理员登录窗口及登录成功后切换主窗口的接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>
namespace Ui { class LoginWindow; }
// 创建用户端 API 客户端并连接 SocketClient 的成功/失败信号。
class ApiClient;
// 实现 QLineEdit 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QLineEdit;
// 创建管理员登录表单、后端地址输入和 API 登录连接。
class LoginWindow : public QWidget
{
    Q_OBJECT
public:
// 创建管理员登录表单、后端地址输入和 API 登录连接。
    explicit LoginWindow(QWidget *parent = nullptr);
// 创建管理员登录表单、后端地址输入和 API 登录连接。
    ~LoginWindow();
private slots:
// 实现 login 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void login();
private:
    Ui::LoginWindow *ui;
    ApiClient *m_api;
    QLineEdit *m_serverEdit;
// 实现 openMainWindow 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void openMainWindow(bool demoMode, const QString &token = QString());
};
#endif
