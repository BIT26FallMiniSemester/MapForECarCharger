// 声明手机号登录页的输入、状态和忙碌状态接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
// 创建手机号登录表单、后端地址输入和登录状态控件。
class LoginPage;
}
QT_END_NAMESPACE

// 创建手机号登录表单、后端地址输入和登录状态控件。
class LoginPage : public QWidget
{
    Q_OBJECT

public:
// 创建手机号登录表单、后端地址输入和登录状态控件。
    explicit LoginPage(QWidget *parent = nullptr);
// 创建手机号登录表单、后端地址输入和登录状态控件。
    ~LoginPage();

// 返回去除首尾空格的手机号输入。
    QString phone() const;
// 返回用户输入的 Qt Socket 后端地址。
    QString apiBaseUrl() const;
// 返回当前客户端的演示模式状态。
    bool demoMode() const;
// 维护请求计数器并统一切换各页面的忙碌状态。
    void setBusy(bool busy);
// 更新登录页状态文字。
    void setStatus(const QString &text);

signals:
// 实现 loginClicked 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void loginClicked();

private:
    Ui::LoginPage *ui;
};

#endif
