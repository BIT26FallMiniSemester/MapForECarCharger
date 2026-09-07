// 声明管理端窗口、图表、表格、筛选器和实时刷新所需的界面状态。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "models.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
// 组装管理端导航、各业务页面、图表和 API 刷新状态。
class MainWindow;
}
QT_END_NAMESPACE

// 创建用户端 API 客户端并连接 SocketClient 的成功/失败信号。
class ApiClient;
// 创建充电页布局、订单操作按钮、站点电桩容器和状态刷新定时器。
class ChargingPage;
// 实现 QPushButton 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QPushButton;

// 组装管理端导航、各业务页面、图表和 API 刷新状态。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
// 组装管理端导航、各业务页面、图表和 API 刷新状态。
    explicit MainWindow(QWidget *parent = nullptr);
// 组装管理端导航、各业务页面、图表和 API 刷新状态。
    ~MainWindow();

private slots:
// 校验手机号、设置后端地址并发起用户登录。
    void onLoginClicked();
// 保存用户资料、进入主界面并加载附近站点。
    void onLoginSucceeded(const QString &token, const User &user, bool isNewUser);
// 按首页位置和范围请求附近充电站。
    void onQueryNearby();
// 打开位置选择对话框并在确认后重新查询站点。
    void onChangeLocation();
// 校验昵称长度并请求保存。
    void onSaveNickname();
// 打开本地图片选择器并显示本地头像。
    void onChooseAvatar();
// 把个人中心输入金额转换为分后发起充值。
    void onRecharge();
// 清除会话和页面状态并返回登录页。
    void onLogout();
// 显示 API 错误，必要时处理会话过期并退出。
    void onApiFailed(int code, const QString &message);
// 维护请求计数器并统一切换各页面的忙碌状态。
    void setBusy(bool busy);

private:
// 设置用户端整体 Qt 样式表和控件状态样式。
    void applyTheme();
// 切换到已登录应用内容。
    void showAppPage();
// 保存用户资料并刷新个人中心和本地头像。
    void applyUser(const User &user);
// 生成按手机号区分的本地头像设置键。
    QString avatarSettingKey() const;

    Ui::MainWindow *ui;
    ApiClient *m_api = nullptr;
    ChargingPage *m_chargingPage = nullptr;
    QPushButton *m_tabCharging = nullptr;
    User m_user;
    int m_inflight = 0;
};

#endif
