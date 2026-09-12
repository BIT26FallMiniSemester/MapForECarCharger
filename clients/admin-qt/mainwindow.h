// 声明管理端窗口、图表、表格、筛选器和实时刷新所需的界面状态。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QList>
#include <QHash>
namespace Ui { class MainWindow; }
// 创建用户端 API 客户端并连接 SocketClient 的成功/失败信号。
class ApiClient;
// 实现 QLabel 的本地处理逻辑，保持与项目其他模块的接口约定一致。
class QLabel;

// 组装管理端导航、各业务页面、图表和 API 刷新状态。
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
// 组装管理端导航、各业务页面、图表和 API 刷新状态。
    explicit MainWindow(bool demoMode = true, const QString &baseUrl = QString(), const QString &token = QString(), QWidget *parent = nullptr);
// 组装管理端导航、各业务页面、图表和 API 刷新状态。
    ~MainWindow();
private:
    Ui::MainWindow *ui;
// 创建运营首页指标卡、趋势图和电桩状态图。
    QWidget *createDashboard();
// 创建电桩表格、筛选器、分页和详情/恢复操作。
    QWidget *createPilePage();
// 创建站点表格、分页及站点编辑和新增电桩操作。
    QWidget *createStationPage();
// 创建用户列表、搜索、详情和冻结/解冻操作。
    QWidget *createUserPage();
// 创建订单管理表格、状态筛选和关键词搜索。
    QWidget *createOrderPage();
// 创建一个带标题、数值和说明的指标卡。
    QWidget *metricCard(const QString &title, const QString &value, const QString &hint);
// 使用演示数据或后端数据绘制营收/订单趋势图。
    void buildTrendChart(int days);
// 根据电桩状态绘制分布图。
    void buildStatusChart();
// 使用演示数据或后端数据绘制营收/订单趋势图。
    void buildTrendChart(const QJsonArray &points);
// 根据电桩状态绘制分布图。
    void buildStatusChart(const QJsonArray &items);
// 加载并渲染当前筛选条件下的电桩列表。
    void refreshPiles();
// 加载并渲染站点列表。
    void refreshStations();
// 加载并渲染用户列表。
    void refreshUsers();
// 加载当前分页和筛选条件下的订单。
    void refreshOrders();
// 按当前页面轮询用户、订单、站点或电桩数据。
    void refreshLiveData();
// 显示选中电桩的统计和状态信息。
    void showPileDetails();
// 校验后端/演示电桩状态并执行恢复操作。
    void restartSelectedPile();
// 显示选中站点详情并同步站点筛选器。
    void showStationDetails();
// 打开新增或编辑站点对话框并提交变更。
    void editStation(bool create);
// 为指定站点创建电桩。
    void addPile();
// 显示用户资料、订单和充值摘要。
    void showUserDetails();
// 切换用户冻结/解冻状态并处理业务限制。
    void toggleUserStatus();
// 并行请求首页概览、趋势和电桩状态数据。
    void loadDashboard(int days = 7);
// 按请求路径把 API 数据分派到对应表格、图表和弹窗。
    void handleApiSuccess(const QString &path, const QJsonValue &data, const QJsonObject &payload);
// 显示请求失败状态并对非列表请求弹出错误提示。
    void handleApiFailure(const QString &path, int httpStatus, int code, const QString &message);
    bool m_demoMode = true;
    ApiClient *m_api = nullptr;
    int m_trendDays = 7;
    int m_pilePage = 1, m_stationPage = 1, m_userPage = 1, m_orderPage = 1;
    int m_pilePages = 1, m_stationPages = 1, m_userPages = 1, m_orderPages = 1;
    QList<QLabel *> m_metricValues;
// 实现 QLabel 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QLabel *m_pilePageLabel = nullptr;
// 实现 QLabel 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QLabel *m_stationPageLabel = nullptr;
// 实现 QLabel 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QLabel *m_userPageLabel = nullptr;
// 实现 QLabel 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QLabel *m_orderPageLabel = nullptr;
    QTableWidget *m_pileTable = nullptr;
    QTableWidget *m_stationTable = nullptr;
    QTableWidget *m_userTable = nullptr;
    QTableWidget *m_orderTable = nullptr;
    QHash<int, QJsonObject> m_stationRows;
    QWidget *m_trendHost = nullptr;
    QWidget *m_statusHost = nullptr;
// 实现 QComboBox 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QComboBox *m_stationFilter = nullptr;
// 实现 QComboBox 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QComboBox *m_statusFilter = nullptr;
// 实现 QLineEdit 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QLineEdit *m_pileSearch = nullptr;
// 实现 QLineEdit 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QLineEdit *m_userSearch = nullptr;
// 实现 QLineEdit 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QLineEdit *m_orderSearch = nullptr;
// 实现 QComboBox 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QComboBox *m_orderStatusFilter = nullptr;
// 实现 QTimer 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    class QTimer *m_refreshTimer = nullptr;
};
#endif
