// 实现管理端首页、运营图表、电桩/站点/用户/订单管理和实时刷新。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "apiclient.h"
#include "chargingpage.h"
#include "homepage.h"
#include "locationdialog.h"
#include "loginpage.h"
#include "navigationdialog.h"
#include "profilepage.h"

#include <QDialog>
#include <QFileDialog>
#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QPushButton>
#include <QScreen>

/// 组装管理端导航、各业务页面、图表和 API 刷新状态。
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_api(new ApiClient(this))
{
    ui->setupUi(this);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        resize(qBound(360, area.width() - 32, 410),
               qBound(640, area.height() - 48, 820));
    }
    applyTheme();

    m_chargingPage = new ChargingPage(m_api, ui->contentStack);
    ui->contentStack->addWidget(m_chargingPage);
    ui->tabHome->setText(QStringLiteral("01  站点"));
    ui->tabMine->setText(QStringLiteral("03  账户"));
    m_tabCharging = new QPushButton(QStringLiteral("02  充电"), ui->bottomBar);
    m_tabCharging->setCheckable(true);
    ui->bottomBarLayout->insertWidget(1, m_tabCharging);

    ui->tabHome->setObjectName(QStringLiteral("tabButton"));
    ui->tabMine->setObjectName(QStringLiteral("tabButton"));
    m_tabCharging->setObjectName(QStringLiteral("tabButton"));
    ui->bottomBar->setObjectName(QStringLiteral("bottomBar"));

    connect(ui->loginPage, &LoginPage::loginClicked, this, &MainWindow::onLoginClicked);
    connect(ui->homePage, &HomePage::queryClicked, this, &MainWindow::onQueryNearby);
    connect(ui->homePage, &HomePage::changeLocationClicked, this, &MainWindow::onChangeLocation);
    connect(ui->homePage,&HomePage::mapZoomRequested,this,[this](double latitude,double longitude,int zoom){m_api->fetchMapSnapshot(latitude,longitude,zoom);});
    connect(ui->homePage, &HomePage::stationSelected, this, [this](const StationSummary &station) {
        m_chargingPage->selectStation(station, ui->homePage->latitude(), ui->homePage->longitude());
        ui->contentStack->setCurrentWidget(m_chargingPage);
        ui->tabHome->setChecked(false);
        m_tabCharging->setChecked(true);
        ui->tabMine->setChecked(false);
    });
    connect(ui->homePage, &HomePage::navigationRequested, this, [this](const StationSummary &station) {
        NavigationDialog dialog(ui->homePage->latitude(), ui->homePage->longitude(), station, this);
        dialog.exec();
    });
    connect(ui->profilePage, &ProfilePage::saveNicknameClicked, this, &MainWindow::onSaveNickname);
    connect(ui->profilePage, &ProfilePage::chooseAvatarClicked, this, &MainWindow::onChooseAvatar);
    connect(ui->profilePage, &ProfilePage::rechargeClicked, this, &MainWindow::onRecharge);
    connect(ui->profilePage, &ProfilePage::orderHistoryClicked, m_api, &ApiClient::fetchOrderHistory);
    connect(ui->profilePage, &ProfilePage::logoutClicked, this, &MainWindow::onLogout);

    connect(ui->tabHome, &QPushButton::clicked, this, [this]() {
        ui->contentStack->setCurrentIndex(0);
        ui->tabHome->setChecked(true);
        m_tabCharging->setChecked(false);
        ui->tabMine->setChecked(false);
    });
    connect(m_tabCharging, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked)
            return;
        ui->contentStack->setCurrentWidget(m_chargingPage);
        ui->tabHome->setChecked(false);
        m_tabCharging->setChecked(true);
        ui->tabMine->setChecked(false);
        m_chargingPage->restoreActiveOrder();
    });
    connect(ui->tabMine, &QPushButton::clicked, this, [this]() {
        ui->contentStack->setCurrentIndex(1);
        ui->tabHome->setChecked(false);
        m_tabCharging->setChecked(false);
        ui->tabMine->setChecked(true);
        m_api->fetchRechargeRecords();
    });

    connect(m_api, &ApiClient::requestStarted, this, [this]() { setBusy(true); });
    connect(m_api, &ApiClient::requestFinished, this, [this]() { setBusy(false); });
    connect(m_api, &ApiClient::loginSucceeded, this, &MainWindow::onLoginSucceeded);
    connect(m_api, &ApiClient::apiFailed, this, &MainWindow::onApiFailed);
    connect(m_api, &ApiClient::nearbyStationsReady, this, [this](const QVector<StationSummary> &stations) {
        ui->homePage->showStations(stations);
        m_api->fetchMapSnapshot(ui->homePage->mapCenterLatitude(),
                                ui->homePage->mapCenterLongitude(),
                                ui->homePage->mapZoom());
    });
    connect(m_api, &ApiClient::mapSnapshotReady, ui->homePage, &HomePage::showMap);
    connect(m_api, &ApiClient::profileReady, this, &MainWindow::applyUser);
    connect(m_api, &ApiClient::avatarUploaded, this, [this](const QString &avatarId) {
        m_user.avatarUrl = avatarId;
        m_api->fetchAvatar(avatarId);
        ui->profilePage->setStatus(QStringLiteral("头像已保存到 Qt 后端"));
    });
    connect(m_api, &ApiClient::avatarReady, ui->profilePage, &ProfilePage::setAvatarData);
    connect(m_chargingPage, &ChargingPage::activeOrderRestored, this,
            [this](const ChargingOrder &) {
        ui->contentStack->setCurrentWidget(m_chargingPage);
        ui->tabHome->setChecked(false);
        m_tabCharging->setChecked(true);
        ui->tabMine->setChecked(false);
    });
    connect(m_api, &ApiClient::nicknameUpdated, this, [this](const User &user) {
        applyUser(user);
        ui->profilePage->setStatus(QStringLiteral("昵称已保存"));
    });
    connect(m_api, &ApiClient::rechargeSucceeded, this, [this](qint64 balanceAfter, const RechargeRecord &) {
        m_user.balanceCents = balanceAfter;
        applyUser(m_user);
        ui->profilePage->setStatus(QStringLiteral("充值成功"));
        m_api->fetchRechargeRecords();
    });
    connect(m_api, &ApiClient::rechargeRecordsReady, ui->profilePage, &ProfilePage::showRechargeRecords);
    connect(m_api, &ApiClient::orderHistoryReady, ui->profilePage, &ProfilePage::showOrderHistory);
    connect(m_api, &ApiClient::balanceChanged, this, [this](qint64 balance) {
        m_user.balanceCents = balance;
        applyUser(m_user);
    });
    connect(m_api, &ApiClient::requestFailed, this,
            [this](const QString &context, int, const QString &message) {
        if (context == QStringLiteral("nearby"))
            ui->homePage->showHint(message);
        else if(context==QStringLiteral("map"))
            ui->homePage->showMapError(message);
        else if(context.startsWith(QStringLiteral("orderHistory:")))
            ui->profilePage->showOrderHistoryError(message);
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

/// 校验手机号、设置后端地址并发起用户登录。
void MainWindow::onLoginClicked()
{
    const QString phone = ui->loginPage->phone();
    if (phone.size() != 11) {
        ui->loginPage->setStatus(QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    ui->loginPage->setStatus(QString());
    m_api->setBaseUrl(ui->loginPage->apiBaseUrl());
    m_api->login(phone);
}

/// 保存用户资料、进入主界面并加载附近站点。
void MainWindow::onLoginSucceeded(const QString &token, const User &user, bool isNewUser)
{
    Q_UNUSED(token);
    applyUser(user);
    showAppPage();
    const QString tip = isNewUser
                            ? QStringLiteral("已自动注册并登录")
                            : QStringLiteral("登录成功");
    ui->homePage->useSimulatedGps();
    ui->homePage->showHint(QStringLiteral("%1，正在加载北京真实站点").arg(tip));
    onQueryNearby();
    m_chargingPage->restoreActiveOrder();
}

/// 按首页位置和范围请求附近充电站。
void MainWindow::onQueryNearby()
{
    ui->homePage->showHint(QStringLiteral("正在查询..."));
    m_api->fetchNearbyStations(ui->homePage->latitude(),
                               ui->homePage->longitude(),
                               ui->homePage->radiusKm());
}

/// 打开位置选择对话框并在确认后重新查询站点。
void MainWindow::onChangeLocation()
{
    LocationDialog dialog(this);
    dialog.setCurrentLocation(ui->homePage->latitude(),
                              ui->homePage->longitude(),
                              ui->homePage->displayName());
    connect(&dialog, &LocationDialog::searchRequested, m_api, &ApiClient::geocode);
    connect(m_api, &ApiClient::locationResolved, &dialog, &LocationDialog::applyGeocodedLocation);
    connect(m_api, &ApiClient::requestFailed, &dialog,
            [&dialog](const QString &context, int, const QString &message) {
        if (context == QStringLiteral("geocode"))
            dialog.showSearchError(message);
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    ui->homePage->setLocation(dialog.latitude(), dialog.longitude(), dialog.displayName());
    onQueryNearby();
}

/// 校验昵称长度并请求保存。
void MainWindow::onSaveNickname()
{
    const QString name = ui->profilePage->nickname();
    if (name.isEmpty() || name.size() > 32) {
        ui->profilePage->setStatus(QStringLiteral("昵称长度需为 1～32"));
        return;
    }
    m_api->updateNickname(name);
}

/// 打开本地图片选择器，压缩后上传到 Qt 后端。
void MainWindow::onChooseAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择头像"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty())
        return;
    QImage image(path);
    if (image.isNull()) {
        ui->profilePage->setStatus(QStringLiteral("无法读取所选图片"));
        return;
    }
    image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray content;
    QBuffer buffer(&content);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 85);
    if (content.size() > 262144) {
        content.clear(); buffer.close(); buffer.open(QIODevice::WriteOnly);
        image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(&buffer, "JPEG", 70);
    }
    if (content.isEmpty() || content.size() > 262144) {
        ui->profilePage->setStatus(QStringLiteral("图片压缩失败，请选择较小的图片"));
        return;
    }
    ui->profilePage->setAvatarData(content);
    ui->profilePage->setStatus(QStringLiteral("正在上传头像…"));
    m_api->uploadAvatar(content, QStringLiteral("image/jpeg"));
}

/// 把个人中心输入金额转换为分后发起充值。
void MainWindow::onRecharge()
{
    m_api->recharge(ui->profilePage->rechargeYuan());
}

/// 清除会话和页面状态并返回登录页。
void MainWindow::onLogout()
{
    m_api->clearSession();
    m_user = User{};
    ui->rootStack->setCurrentIndex(0);
    ui->contentStack->setCurrentIndex(0);
    ui->tabHome->setChecked(true);
    m_tabCharging->setChecked(false);
    ui->tabMine->setChecked(false);
    m_chargingPage->clearSession();
    ui->loginPage->setStatus(QString());
}

/// 显示 API 错误，必要时处理会话过期并退出。
void MainWindow::onApiFailed(int code, const QString &message)
{
    if (ui->rootStack->currentIndex() == 0)
        ui->loginPage->setStatus(message);
    else
        ui->profilePage->setStatus(message);

    if (code == 40101)
        onLogout();
}

/// 维护请求计数器并统一切换各页面的忙碌状态。
void MainWindow::setBusy(bool busy)
{
    m_inflight = busy ? m_inflight + 1 : qMax(0, m_inflight - 1);
    const bool on = m_inflight > 0;
    ui->loginPage->setBusy(on && ui->rootStack->currentIndex() == 0);
}

/// 切换到已登录应用内容。
void MainWindow::showAppPage()
{
    ui->rootStack->setCurrentIndex(1);
    ui->contentStack->setCurrentIndex(0);
    ui->tabHome->setChecked(true);
    m_tabCharging->setChecked(false);
    ui->tabMine->setChecked(false);
}

/// 保存用户资料并刷新个人中心和后端头像。
void MainWindow::applyUser(const User &user)
{
    m_user = user;
    ui->profilePage->setUser(user);
    if (!user.avatarUrl.isEmpty())
        m_api->fetchAvatar(user.avatarUrl);
}

/// 设置用户端整体 Qt 样式表和控件状态样式。
void MainWindow::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QDialog, QWidget {
            background: #ffffff;
            color: #17302b;
            font-family: "Noto Sans CJK SC", "Microsoft YaHei", sans-serif;
            font-size: 15px;
        }
        QLabel { background: transparent; }
        #brandLabel {
            color: #008c68;
            font-family: "DejaVu Sans Mono", monospace;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 2px;
        }
        #titleLabel { color: #102a25; font-size: 25px; font-weight: 800; }
        #subtitleLabel, #hintLabel, #cardInfo { color: #5e756f; font-size: 13px; }
        #statusLabel { color: #c23b33; font-size: 13px; }
        #cardTitle { color: #17302b; font-size: 16px; font-weight: 700; }
        #card {
            background: #f7faf9;
            border: 1px solid #d6e4e0;
            border-left: 3px solid #00a878;
            border-radius: 3px;
        }
        QFrame#stationCard {
            background: #ffffff;
            border: 1px solid #d6e4e0;
            border-left: 3px solid #aac3bc;
            border-radius: 2px;
        }
        QFrame#stationCard:hover { background: #f3f8fc; border-color: #2c88b0; }
        QFrame#stationCard[selected="true"] {
            background: #ecfaf5;
            border: 1px solid #00a878;
            border-left: 6px solid #f2b134;
        }
        #bottomBar {
            background: #ffffff;
            border-top: 1px solid #d6e4e0;
            min-height: 62px;
            max-height: 62px;
        }
        #radiusBox { background: transparent; }
        QLineEdit, QDoubleSpinBox, QComboBox, QComboBox QAbstractItemView {
            background: #ffffff;
            border: 1px solid #c8dad5;
            border-radius: 3px;
            padding: 9px 11px;
            color: #17302b;
            min-height: 25px;
            selection-background-color: #bdebdc;
        }
        QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid #00a878; }
        QComboBox { padding: 9px 36px 9px 11px; }
        QComboBox QAbstractItemView {
            background: #ffffff;
            border: 1px solid #c8dad5;
            selection-background-color: #e2f5ee;
            color: #17302b;
        }
        QToolButton#stepButton {
            background: #f2b134;
            color: #2d2107;
            border: 1px solid #f2b134;
            border-radius: 2px;
            min-width: 36px;
            min-height: 22px;
            font-size: 11px;
            font-weight: 700;
        }
        QToolButton#stepButton:hover { background: #f8ca63; }
        QToolButton#stepButton:disabled { background: #edf2f0; color: #91a49f; border-color: #d6e4e0; }
        QPushButton {
            background: #00a878;
            color: #ffffff;
            border: 1px solid #00a878;
            border-radius: 3px;
            padding: 10px 12px;
            min-height: 26px;
            font-weight: 700;
        }
        QPushButton:hover { background: #00bd88; border-color: #00bd88; }
        QPushButton:pressed { background: #008c68; border-color: #008c68; }
        QPushButton:focus { border: 2px solid #f2b134; }
        QPushButton:disabled { background: #edf2f0; color: #91a49f; border-color: #d6e4e0; }
        QPushButton#locationButton {
            background: #f3f8fc;
            color: #245f7a;
            text-align: left;
            border: 1px solid #bcd5e1;
            border-left: 4px solid #2c88b0;
        }
        QPushButton#locationButton:hover { background: #e7f2f7; border-color: #2c88b0; }
        QPushButton#dangerButton { background: #e1544b; color: #ffffff; border-color: #e1544b; }
        QPushButton#dangerButton:hover { background: #c9433b; }
        QPushButton#tabButton {
            background: transparent;
            color: #69807a;
            border: none;
            border-top: 2px solid transparent;
            border-radius: 0;
            padding: 9px 6px 7px 6px;
            min-height: 30px;
            font-family: "DejaVu Sans Mono", "Noto Sans CJK SC", monospace;
            font-size: 12px;
        }
        QPushButton#tabButton:hover { color: #17302b; background: #f3f7f6; }
        QPushButton#tabButton:checked { color: #008c68; background: #ecfaf5; border-top: 2px solid #00a878; }
        QPushButton#pileButton {
            background: #ffffff;
            color: #29453f;
            border: 1px solid #c8dad5;
            border-left: 3px solid #00a878;
            border-radius: 2px;
            text-align: left;
            font-family: "DejaVu Sans Mono", "Noto Sans CJK SC", monospace;
            font-size: 12px;
        }
        QPushButton#pileButton:hover { background: #f3f8fc; border-color: #2c88b0; }
        QPushButton#pileButton:checked { background: #00a878; color: #ffffff; border: 2px solid #f2b134; }
        QPushButton#pileButton:disabled { background: #f4f6f5; color: #9aaba6; border-color: #dde7e4; border-left: 3px solid #c9d5d2; }
        QPushButton#secondaryButton { background: #f3f8fc; color: #245f7a; border-color: #bcd5e1; }
        QPushButton#secondaryButton:hover { background: #e7f2f7; border-color: #2c88b0; }
        QFrame#orderCard { background:#ffffff; border:1px solid #d6e4e0; border-left:4px solid #aac3bc; border-radius:2px; }
        QFrame#orderCard[orderState="COMPLETED"] { border-left-color:#00a878; }
        QFrame#orderCard[orderState="CHARGING"], QFrame#orderCard[orderState="RESERVED"] { border-left-color:#2c88b0; }
        QFrame#orderCard[orderState="UNPAID"] { border-left-color:#f2b134; }
        QFrame#orderCard[orderState="CANCELLED"] { border-left-color:#e1544b; }
        #orderNumber { color:#29453f; font-family:"DejaVu Sans Mono", monospace; font-size:12px; font-weight:700; }
        #orderStatus { color:#5e756f; font-family:"DejaVu Sans Mono", "Noto Sans CJK SC", monospace; font-size:12px; font-weight:700; }
        #orderStatus[orderState="COMPLETED"] { color:#008c68; }
        #orderStatus[orderState="CHARGING"], #orderStatus[orderState="RESERVED"] { color:#245f7a; }
        #orderStatus[orderState="UNPAID"] { color:#9a6500; }
        #orderStatus[orderState="CANCELLED"] { color:#c23b33; }
        #chargeCard {
            background: #f2fbf7;
            color: #17302b;
            border: 1px solid #c8dad5;
            border-top: 4px solid #00a878;
            border-radius: 2px;
        }
        #chargeCard #cardTitle, #chargeStatus { color: #17302b; }
        #chargeMetrics { color: #008c68; font-family: "DejaVu Sans Mono", monospace; font-size: 18px; font-weight: 700; }
        #routeInfo { color: #9a6500; font-family: "DejaVu Sans Mono", monospace; font-weight: 600; }
        #mapSelection { background:#ffffff; border:1px solid #bcd5e1; border-left:4px solid #2c88b0; border-radius:2px; }
        #mapDetail { color:#245f7a; font-size:12px; font-family:"DejaVu Sans Mono", "Noto Sans CJK SC", monospace; }
        #avatar { background:#eaf8f3; color:#008c68; border:2px solid #00a878; }
        QToolButton#mapZoomButton { background:#ffffff;color:#008c68;border:1px solid #00a878;border-radius:2px;font-size:21px;font-weight:700; }
        QToolButton#mapZoomButton:hover { background:#eaf8f3; }
        QCheckBox { color: #5e756f; spacing: 8px; }
        QScrollArea { background: transparent; border: none; }
        QScrollArea > QWidget > QWidget { background: #ffffff; }
        QScrollBar:vertical { background: #f1f5f4; width: 6px; margin: 0; }
        QScrollBar::handle:vertical { background: #aac3bc; border-radius: 0; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QToolTip { background:#ffffff; color:#17302b; border:1px solid #2c88b0; padding:6px; }
    )"));
}
