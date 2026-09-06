#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "apiclient.h"
#include "chargingpage.h"
#include "homepage.h"
#include "locationdialog.h"
#include "loginpage.h"
#include "profilepage.h"

#include <QDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_api(new ApiClient(this))
{
    ui->setupUi(this);
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
    connect(ui->homePage, &HomePage::stationSelected, this, [this](const StationSummary &station) {
        m_chargingPage->selectStation(station, ui->homePage->latitude(), ui->homePage->longitude());
        ui->contentStack->setCurrentWidget(m_chargingPage);
        ui->tabHome->setChecked(false);
        m_tabCharging->setChecked(true);
        ui->tabMine->setChecked(false);
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
    connect(m_api, &ApiClient::nearbyStationsReady, ui->homePage, &HomePage::showStations);
    connect(m_api, &ApiClient::profileReady, this, &MainWindow::applyUser);
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
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

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

void MainWindow::onLoginSucceeded(const QString &token, const User &user, bool isNewUser)
{
    Q_UNUSED(token);
    applyUser(user);
    showAppPage();
    const QString tip = isNewUser
                            ? QStringLiteral("已自动注册并登录")
                            : QStringLiteral("登录成功");
    QMessageBox::information(this, QStringLiteral("欢迎"),
                             tip + QStringLiteral("\n%1").arg(user.nickname));
    ui->homePage->useSimulatedGps();
    ui->homePage->showHint(QStringLiteral("Qt 后端已连接，使用北京演示定位"));
    onQueryNearby();
    m_chargingPage->restoreActiveOrder();
}

void MainWindow::onQueryNearby()
{
    ui->homePage->showHint(QStringLiteral("正在查询..."));
    m_api->fetchNearbyStations(ui->homePage->latitude(),
                               ui->homePage->longitude(),
                               ui->homePage->radiusKm());
}

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

void MainWindow::onSaveNickname()
{
    const QString name = ui->profilePage->nickname();
    if (name.isEmpty() || name.size() > 32) {
        ui->profilePage->setStatus(QStringLiteral("昵称长度需为 1～32"));
        return;
    }
    m_api->updateNickname(name);
}

void MainWindow::onChooseAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择头像"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty())
        return;
    QSettings settings;
    settings.setValue(avatarSettingKey(), path);
    ui->profilePage->setAvatarPath(path);
    ui->profilePage->setStatus(QStringLiteral("已使用本地头像（稍后对接上传接口）"));
}

void MainWindow::onRecharge()
{
    m_api->recharge(ui->profilePage->rechargeYuan());
}

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

void MainWindow::onApiFailed(int code, const QString &message)
{
    if (ui->rootStack->currentIndex() == 0)
        ui->loginPage->setStatus(message);
    else
        ui->profilePage->setStatus(message);

    if (code == 40101)
        onLogout();
}

void MainWindow::setBusy(bool busy)
{
    m_inflight = busy ? m_inflight + 1 : qMax(0, m_inflight - 1);
    const bool on = m_inflight > 0;
    ui->loginPage->setBusy(on);
    ui->homePage->setBusy(on);
    ui->profilePage->setBusy(on);
    m_chargingPage->setBusy(on);
}

void MainWindow::showAppPage()
{
    ui->rootStack->setCurrentIndex(1);
    ui->contentStack->setCurrentIndex(0);
    ui->tabHome->setChecked(true);
    m_tabCharging->setChecked(false);
    ui->tabMine->setChecked(false);
}

void MainWindow::applyUser(const User &user)
{
    m_user = user;
    ui->profilePage->setUser(user);
    const QString localAvatar = QSettings().value(avatarSettingKey()).toString();
    if (!localAvatar.isEmpty())
        ui->profilePage->setAvatarPath(localAvatar);
}

QString MainWindow::avatarSettingKey() const
{
    return QStringLiteral("avatar/") + m_user.phone;
}

void MainWindow::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #f4f7f2;
            color: #1f3d36;
            font-family: "Microsoft YaHei", "Noto Sans CJK SC", sans-serif;
            font-size: 14px;
        }
        QLabel { background: transparent; }
        #brandLabel { color: #0f766e; font-size: 13px; font-weight: 700; letter-spacing: 1px; }
        #titleLabel { color: #134e4a; font-size: 24px; font-weight: 700; }
        #subtitleLabel, #hintLabel, #cardInfo { color: #5b6f69; font-size: 13px; }
        #statusLabel { color: #dc2626; font-size: 13px; }
        #cardTitle { color: #134e4a; font-size: 16px; font-weight: 600; }
        #card {
            background: #ffffff;
            border: 1px solid #d7ebe4;
            border-left: 5px solid #14b8a6;
            border-radius: 14px;
        }
        #bottomBar { background: #134e4a; }
        #radiusBox { background: transparent; }
        QLineEdit, QDoubleSpinBox, QComboBox, QComboBox QAbstractItemView {
            background: #ffffff;
            border: 1px solid #c5ddd6;
            border-radius: 12px;
            padding: 8px 12px;
            color: #134e4a;
            min-height: 22px;
            selection-background-color: #99f6e4;
        }
        QComboBox {
            padding: 10px 44px 10px 12px;
            min-height: 40px;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 40px;
            border: none;
            border-left: 1px solid #0f766e;
            border-top-right-radius: 12px;
            border-bottom-right-radius: 12px;
            background: #0d9488;
        }
        QComboBox::down-arrow {
            width: 0;
            height: 0;
            border-left: 6px solid transparent;
            border-right: 6px solid transparent;
            border-top: 8px solid #ffffff;
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
            font-weight: 600;
        }
        QPushButton:hover { background: #0f766e; }
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
            padding: 12px;
        }
        QPushButton#tabButton:checked { color: #fde68a; }
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
        QCheckBox { color: #3f5c55; spacing: 8px; }
        QScrollArea { background: transparent; border: none; }
        QDialog { background: #f4f7f2; }
    )"));
}
