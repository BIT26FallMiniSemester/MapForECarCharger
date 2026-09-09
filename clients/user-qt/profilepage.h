// 声明个人中心、昵称、头像、充值和充值记录界面接口。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef PROFILEPAGE_H
#define PROFILEPAGE_H

#include "models.h"

#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
// 创建个人中心表单、余额、头像、充值记录和操作按钮。
class ProfilePage;
}
QT_END_NAMESPACE

class QPushButton;

// 创建个人中心表单、余额、头像、充值记录和操作按钮。
class ProfilePage : public QWidget
{
    Q_OBJECT

public:
// 创建个人中心表单、余额、头像、充值记录和操作按钮。
    explicit ProfilePage(QWidget *parent = nullptr);
// 创建个人中心表单、余额、头像、充值记录和操作按钮。
    ~ProfilePage();

// 返回清理后的昵称输入。
    QString nickname() const;
// 返回用户输入的充值金额。
    double rechargeYuan() const;
// 把用户模型渲染到个人中心。
    void setUser(const User &user);
// 从本地文件加载并缩放头像。
    void setAvatarPath(const QString &path);
    void setAvatarData(const QByteArray &content);
// 维护请求计数器并统一切换各页面的忙碌状态。
    void setBusy(bool busy);
// 把充值记录渲染为列表。
    void showRechargeRecords(const QVector<RechargeRecord> &records);
    void showOrderHistory(const QVector<ChargingOrder> &orders);
    void showOrderHistoryError(const QString &message);
// 更新登录页状态文字。
    void setStatus(const QString &text);

signals:
// 实现 saveNicknameClicked 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void saveNicknameClicked();
// 实现 chooseAvatarClicked 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void chooseAvatarClicked();
// 实现 rechargeClicked 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void rechargeClicked();
    void orderHistoryClicked();
// 实现 logoutClicked 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void logoutClicked();

private:
    Ui::ProfilePage *ui;
    QPushButton *m_historyButton = nullptr;
};

#endif
