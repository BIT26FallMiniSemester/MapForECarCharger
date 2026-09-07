// 声明位置选择对话框、区域快捷项、地址搜索和位置结果槽。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#ifndef LOCATIONDIALOG_H
#define LOCATIONDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
// 创建位置选择对话框并加载区域快捷项。
class LocationDialog;
}
QT_END_NAMESPACE

// 创建位置选择对话框并加载区域快捷项。
class LocationDialog : public QDialog
{
    Q_OBJECT

public:
// 创建位置选择对话框并加载区域快捷项。
    explicit LocationDialog(QWidget *parent = nullptr);
// 创建位置选择对话框并加载区域快捷项。
    ~LocationDialog();

    double latitude() const { return m_lat; }
    double longitude() const { return m_lng; }
    QString displayName() const { return m_displayName; }

// 初始化位置选择对话框的当前坐标和名称。
    void setCurrentLocation(double lat, double lng, const QString &displayName);

public slots:
// 接收地图服务返回的地址坐标并更新对话框。
    void applyGeocodedLocation(double lat, double lng, const QString &displayName);
// 显示地址搜索失败信息并恢复搜索按钮。
    void showSearchError(const QString &message);

signals:
// 实现 searchRequested 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void searchRequested(const QString &address);

private slots:
// 应用当前下拉区域。
    void onUseRegion();
// 校验地址并发出地图搜索请求。
    void onSearch();
// 确认并关闭位置选择对话框。
    void onAccepted();

private:
// 保存位置坐标和显示名称。
    void applyLocation(double lat, double lng, const QString &name);
// 向区域下拉框加入北京市中心和常用区域快捷项。
    void fillRegions();

    Ui::LocationDialog *ui;
    double m_lat = 39.9042;
    double m_lng = 116.4074;
// 实现 QStringLiteral 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    QString m_displayName = QStringLiteral("北京市中心");
    bool m_searching = false;
};

#endif
