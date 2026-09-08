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
    m_tabCharging = new QPushButton(QStringLiteral("充电"), ui->bottomBar);
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
        QMainWindow, QWidget {
            background: #f6f8f7;
            color: #172b26;
            font-family: "Microsoft YaHei", "Noto Sans CJK SC", sans-serif;
            font-size: 15px;
        }
        QLabel { background: transparent; }
        #brandLabel { color: #0f766e; font-size: 13px; font-weight: 700; letter-spacing: 1px; }
        #titleLabel { color: #123f38; font-size: 22px; font-weight: 700; }
        #subtitleLabel, #hintLabel, #cardInfo { color: #4b625c; font-size: 14px; }
        #statusLabel { color: #b42318; font-size: 14px; }
        #cardTitle { color: #134e4a; font-size: 16px; font-weight: 600; }
        #card {
            background: #ffffff;
            border: 1px solid #d7ebe4;
            border-left: 4px solid #0d9488;
            border-radius: 12px;
        }
        QFrame#stationCard {
            background: #ffffff;
            border: 1px solid #d7ebe4;
            border-left: 4px solid #a7d9ce;
            border-radius: 12px;
        }
        QFrame#stationCard:hover { background: #f0fdfa; border-color: #5eead4; }
        QFrame#stationCard[selected="true"] { background: #ecfdf5; border: 2px solid #0d9488; border-left: 6px solid #f59e0b; }
        #bottomBar { background: #123f38; min-height: 64px; max-height: 64px; }
        #radiusBox { background: transparent; }
        QLineEdit, QDoubleSpinBox, QComboBox, QComboBox QAbstractItemView {
            background: #ffffff;
            border: 1px solid #c5ddd6;
            border-radius: 12px;
            padding: 8px 12px;
            color: #134e4a;
            min-height: 24px;
            selection-background-color: #99f6e4;
        }
        QComboBox {
            padding: 8px 44px 8px 12px;
            min-height: 24px;
        }
        QComboBox QAbstractItemView {
            background: #ffffff;
            border: 1px solid #c5ddd6;
            selection-background-color: #ccfbf1;
            color: #134e4a;
        }
        QToolButton#stepButton {
            background: #f59e0b;
            color: #422006;
            border: none;
            border-radius: 8px;
            min-width: 36px;
            min-height: 22px;
            font-size: 11px;
            font-weight: 700;
        }
        QToolButton#stepButton:hover { background: #d97706; }
        QToolButton#stepButton:disabled { background: #e7e5e4; color: #a8a29e; }
        QPushButton {
            background: #0d9488;
            color: white;
            border: none;
            border-radius: 12px;
            padding: 10px;
            min-height: 24px;
            font-weight: 600;
        }
        QPushButton:hover { background: #0f766e; }
        QPushButton:pressed { background: #115e59; }
        QPushButton:focus, QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 2px solid #f59e0b; }
        QPushButton:disabled { background: #e2e8f0; color: #94a3b8; }
        QPushButton#locationButton {
            background: #fff7ed;
            color: #9a3412;
            text-align: left;
            border: 1px solid #fdba74;
        }
        QPushButton#locationButton:hover { background: #ffedd5; }
        QPushButton#dangerButton { background: #e11d48; }
        QPushButton#dangerButton:hover { background: #be123c; }
        QPushButton#tabButton {
            background: transparent;
            color: #99f6e4;
            border-radius: 10px;
            padding: 8px 12px;
            min-height: 28px;
        }
        QPushButton#tabButton:checked { color: #fff7d6; background: #0f766e; }
        QPushButton#pileButton {
            background: #ecfdf5;
            color: #115e59;
            border: 1px solid #99f6e4;
            text-align: left;
        }
        QPushButton#pileButton:hover { background: #ccfbf1; }
        QPushButton#pileButton:disabled { background: #f5f5f4; color: #a8a29e; border-color: #e7e5e4; }
        QPushButton#secondaryButton { background: #e2e8f0; color: #334155; }
        QPushButton#secondaryButton:hover { background: #cbd5e1; }
        #chargeCard {
            background: #0f766e;
            color: white;
            border-radius: 16px;
        }
        #chargeCard #cardTitle, #chargeStatus, #chargeMetrics { color: white; }
        #chargeMetrics { font-size: 18px; font-weight: 700; line-height: 1.5; }
        #routeInfo { color: #b45309; font-weight: 600; }
        #mapSelection { background:#ffffff; border:1px solid #c9e4dc; border-radius:12px; }
        #mapDetail { color:#315b54; font-size:13px; }
        QToolButton#mapZoomButton { background:#ffffff;color:#134e4a;border:1px solid #7bb8a9;border-radius:10px;font-size:22px;font-weight:700; }
        QToolButton#mapZoomButton:hover { background:#ccfbf1; }
        QCheckBox { color: #3f5c55; spacing: 8px; }
        QScrollArea { background: transparent; border: none; }
        QScrollArea > QWidget > QWidget { background: transparent; }
        QScrollBar:vertical { background: transparent; width: 7px; margin: 0; }
        QScrollBar::handle:vertical { background: #a7c7be; border-radius: 3px; min-height: 28px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QDialog { background: #f6f8f7; }
    )"));
}
