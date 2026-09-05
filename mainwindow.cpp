#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "apiclient.h"
#include "homepage.h"
#include "locationdialog.h"
#include "loginpage.h"
#include "mapconfig.h"
#include "navigationdialog.h"
#include "orderdialog.h"
#include "profilepage.h"
#include "editprofiledialog.h"
#include "recordlistdialog.h"
#include "rechargedialog.h"
#include "stationdetaildialog.h"
#include "tencentgeocoder.h"

#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_api(new ApiClient(this))
    , m_geo(new TencentGeocoder(this))
{
    ui->setupUi(this);
    applyTheme();
    m_geo->setApiKey(tencentMapKey());

    ui->tabHome->setObjectName(QStringLiteral("tabButton"));
    ui->tabMine->setObjectName(QStringLiteral("tabButton"));
    ui->bottomBar->setObjectName(QStringLiteral("bottomBar"));

    connect(ui->loginPage, &LoginPage::loginClicked, this, &MainWindow::onLoginClicked);
    connect(ui->homePage, &HomePage::queryClicked, this, &MainWindow::onQueryNearby);
    connect(ui->homePage, &HomePage::searchClicked, this, &MainWindow::onSearchStations);
    connect(ui->homePage, &HomePage::changeLocationClicked, this, &MainWindow::onChangeLocation);
    connect(ui->homePage, &HomePage::stationClicked, this, &MainWindow::onStationClicked);
    connect(ui->homePage, &HomePage::activeOrderClicked, this, &MainWindow::onActiveOrderClicked);
    connect(ui->profilePage, &ProfilePage::editClicked, this, &MainWindow::onEditProfile);
    connect(ui->profilePage, &ProfilePage::rechargeClicked, this, &MainWindow::onRecharge);
    connect(ui->profilePage, &ProfilePage::rechargeRecordsClicked, this, &MainWindow::onRechargeRecords);
    connect(ui->profilePage, &ProfilePage::orderRecordsClicked, this, &MainWindow::onOrderRecords);
    connect(ui->profilePage, &ProfilePage::activeOrderClicked, this, &MainWindow::onActiveOrderClicked);
    connect(ui->profilePage, &ProfilePage::logoutClicked, this, &MainWindow::onLogout);

    connect(ui->tabHome, &QPushButton::clicked, this, [this]() {
        ui->contentStack->setCurrentIndex(0);
        ui->tabHome->setChecked(true);
        ui->tabMine->setChecked(false);
    });
    connect(ui->tabMine, &QPushButton::clicked, this, [this]() {
        ui->contentStack->setCurrentIndex(1);
        ui->tabHome->setChecked(false);
        ui->tabMine->setChecked(true);
    });

    connect(m_api, &ApiClient::requestStarted, this, [this]() { setBusy(true); });
    connect(m_api, &ApiClient::requestFinished, this, [this]() { setBusy(false); });
    connect(m_api, &ApiClient::loginSucceeded, this, &MainWindow::onLoginSucceeded);
    connect(m_api, &ApiClient::apiFailed, this, &MainWindow::onApiFailed);
    connect(m_api, &ApiClient::nearbyStationsReady, ui->homePage, &HomePage::showStations);
    connect(m_api, &ApiClient::stationFilterOptionsReady, ui->homePage, &HomePage::setFilterOptions);
    connect(m_api, &ApiClient::stationsSearchReady, ui->homePage, &HomePage::showSearchStations);
    connect(m_api, &ApiClient::profileReady, this, &MainWindow::applyUser);
    connect(m_api, &ApiClient::nicknameUpdated, this, [this](const User &user) {
        applyUser(user);
        ui->profilePage->setStatus(QStringLiteral("资料已保存"), true);
    });
    connect(m_api, &ApiClient::rechargeSucceeded, this, [this](qint64 balanceAfter, const RechargeRecord &) {
        m_user.balanceCents = balanceAfter;
        applyUser(m_user);
        ui->profilePage->setStatus(QStringLiteral("充值成功"), true);
        if (m_orderDialog)
            m_orderDialog->setBalanceCents(m_user.balanceCents);
    });
    connect(m_api, &ApiClient::avatarUploaded, this, [this](const User &user) {
        applyUser(user);
        if (!m_pendingNickname.isEmpty()) {
            const QString nick = m_pendingNickname;
            m_pendingNickname.clear();
            m_api->updateNickname(nick);
            return;
        }
        ui->profilePage->setStatus(QStringLiteral("资料已保存"), true);
    });
    connect(m_api, &ApiClient::stationDetailReady, this, [this](const StationSummary &station) {
        if (m_stationDialog)
            m_stationDialog->setStation(station);
    });
    connect(m_api, &ApiClient::pileDetailReady, this, [this](const ChargingPile &pile) {
        QMessageBox::information(m_stationDialog ? static_cast<QWidget *>(m_stationDialog.data())
                                                 : this,
                                 QStringLiteral("电桩详情"),
                                 QStringLiteral("编号：%1\n类型：%2\n功率：%3\n状态：%4\n累计充电：%5 次\n累计时长：%6")
                                     .arg(pile.pileNo,
                                          pileTypeText(pile.pileType),
                                          powerText(pile.ratedPowerW),
                                          pileStatusText(pile.status))
                                     .arg(pile.totalChargeCount)
                                     .arg(formatDurationSeconds(pile.totalChargeDurationSeconds)));
    });
    connect(m_api, &ApiClient::activeOrderReady, this, [this](bool hasOrder, const ChargingOrder &order) {
        applyActiveOrder(hasOrder, order);
        if (m_openActiveWhenReady) {
            m_openActiveWhenReady = false;
            if (hasOrder)
                openOrderDialog(order, m_stationDialog ? static_cast<QWidget *>(m_stationDialog.data()) : this);
            else
                QMessageBox::information(this, QStringLiteral("当前订单"), QStringLiteral("当前没有未完成订单。"));
        }
    });
    connect(m_api, &ApiClient::orderCreated, this, [this](const ChargingOrder &order) {
        m_createdOrder = order;
        applyActiveOrder(true, order);
        if (m_autoReserve) {
            m_autoReserve = false;
            m_api->reserveOrder(order.id);
            return;
        }
        openOrderDialog(order, m_stationDialog ? static_cast<QWidget *>(m_stationDialog.data()) : this);
    });
    connect(m_api, &ApiClient::orderUpdated, this, [this](const ChargingOrder &order) {
        m_createdOrder = order;
        applyActiveOrder(isActiveOrderStatus(order.status), order);
        if (m_orderDialog) {
            m_orderDialog->setOrder(order);
            m_orderDialog->setBalanceCents(m_user.balanceCents);
        } else if (m_stationDialog && (order.status == QLatin1String("RESERVED")
                                       || order.status == QLatin1String("PENDING"))) {
            openOrderDialog(order, m_stationDialog);
        }
        refreshStationPiles();
    });
    connect(m_api, &ApiClient::orderSettled, this, [this](const ChargingOrder &order, qint64 balanceCents) {
        m_user.balanceCents = balanceCents;
        applyUser(m_user);
        applyActiveOrder(false, ChargingOrder{});
        if (m_orderDialog) {
            ChargingOrder shown = m_orderDialog->order();
            if (order.id != 0)
                shown.id = order.id;
            if (!order.status.isEmpty())
                shown.status = order.status;
            if (order.amountCents > 0)
                shown.amountCents = order.amountCents;
            if (!order.settledAt.isEmpty())
                shown.settledAt = order.settledAt;
            if (!order.orderNo.isEmpty())
                shown.orderNo = order.orderNo;
            m_orderDialog->setOrder(shown);
            m_orderDialog->setBalanceCents(balanceCents);
            m_orderDialog->setHint(QStringLiteral("结算成功，余额已更新"), true);
        }
        refreshStationPiles();
        QMessageBox::information(m_orderDialog ? static_cast<QWidget *>(m_orderDialog.data()) : this,
                                 QStringLiteral("结算成功"),
                                 QStringLiteral("已扣款 ¥%1，当前余额 ¥%2")
                                     .arg(centsToYuanText(order.amountCents > 0 ? order.amountCents : 0),
                                          centsToYuanText(balanceCents)));
        if (order.id > 0)
            m_api->fetchOrderDetail(order.id);
        m_api->fetchProfile();
    });
    connect(m_geo, &TencentGeocoder::geocodeSucceeded, this, &MainWindow::onSelfLocated);
    connect(m_geo, &TencentGeocoder::geocodeFailed, this, &MainWindow::onSelfLocateFailed);
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
    m_api->setDemoMode(ui->loginPage->demoMode());
    m_api->setBaseUrl(ui->loginPage->apiBaseUrl());
    m_api->login(phone);
}

void MainWindow::onLoginSucceeded(const QString &token, const User &user, bool isNewUser)
{
    Q_UNUSED(token);
    applyUser(user);
    showAppPage();
    ui->homePage->resetToDistanceMode();
    const QString tip = isNewUser
                            ? QStringLiteral("已自动注册并登录")
                            : QStringLiteral("登录成功");
    QMessageBox::information(this, QStringLiteral("欢迎"),
                             tip + QStringLiteral("\n%1").arg(user.nickname));
    ui->homePage->showHint(QStringLiteral("正在定位当前位置…"));
    ui->homePage->setActiveOrder(false, ChargingOrder{});
    ui->profilePage->setActiveOrder(false, ChargingOrder{});
    m_api->fetchStationFilterOptions();
    m_api->fetchActiveOrder();
    m_geo->locateByIp();
}

void MainWindow::onSelfLocated(double lat, double lng, const QString &name)
{
    ui->homePage->setLocation(lat, lng, name);
    if (!ui->homePage->isKeywordMode())
        onQueryNearby();
}

void MainWindow::onSelfLocateFailed(const QString &message)
{
    ui->homePage->useSimulatedGps();
    if (ui->homePage->isKeywordMode())
        return;
    ui->homePage->showHint(message + QStringLiteral("，已改用模拟定位"));
    onQueryNearby();
}

void MainWindow::onQueryNearby()
{
    ui->homePage->showHint(QStringLiteral("正在查询..."));
    m_api->fetchNearbyStations(ui->homePage->latitude(),
                               ui->homePage->longitude(),
                               ui->homePage->radiusKm(),
                               ui->homePage->availableOnly());
}

void MainWindow::onSearchStations()
{
    ui->homePage->showHint(QStringLiteral("正在搜索..."));
    m_api->fetchStations(ui->homePage->keyword(),
                         ui->homePage->selectedDistrict(),
                         ui->homePage->selectedOperator(),
                         ui->homePage->selectedRegionScope(),
                         ui->homePage->selectedLocationType(),
                         ui->homePage->bookableOnly());
}

void MainWindow::onChangeLocation()
{
    LocationDialog dialog(this);
    dialog.setCurrentLocation(ui->homePage->latitude(),
                              ui->homePage->longitude(),
                              ui->homePage->displayName());
    if (dialog.exec() != QDialog::Accepted)
        return;
    ui->homePage->setLocation(dialog.latitude(), dialog.longitude(), dialog.displayName());
    onQueryNearby();
}

void MainWindow::onStationClicked(const StationSummary &station)
{
    StationDetailDialog detail(this);
    m_stationDialog = &detail;
    detail.setStation(station);
    connect(m_api, &ApiClient::stationPilesReady, &detail, &StationDetailDialog::showPiles);
    connect(m_api, &ApiClient::apiFailed, &detail,
            [&detail](int, const QString &message) { detail.showPilesHint(message); });
    connect(&detail, &StationDetailDialog::navigateClicked, this, [this, &detail]() {
        const StationSummary current = detail.station();
        NavigationDialog nav(&detail);
        nav.setDestination(current);
        nav.setOrigin(ui->homePage->latitude(),
                      ui->homePage->longitude(),
                      ui->homePage->displayName());
        nav.exec();
    });
    connect(&detail, &StationDetailDialog::reserveClicked, this, [this, &detail](const ChargingPile &pile) {
        if (m_hasActiveOrder) {
            warnUnfinishedOrder(&detail);
            openOrderDialog(m_activeOrder, &detail);
            return;
        }
        m_createdOrder = ChargingOrder{};
        m_autoReserve = true;
        m_api->createOrder(pile.id);
    });
    connect(&detail, &StationDetailDialog::pileClicked, this, [this](const ChargingPile &pile) {
        m_api->fetchPileDetail(pile.id);
    });
    m_api->fetchStationDetail(station.id);
    m_api->fetchStationPiles(station.id);
    detail.exec();
    m_stationDialog.clear();
}

void MainWindow::onEditProfile()
{
    EditProfileDialog dialog(this);
    dialog.setNickname(m_user.nickname);
    dialog.setAvatarPath(QSettings().value(avatarSettingKey()).toString());
    if (dialog.exec() != QDialog::Accepted)
        return;

    ui->profilePage->setStatus(QString());
    m_pendingNickname.clear();
    const QString nick = dialog.nickname();
    const bool nickChanged = (nick != m_user.nickname);
    if (dialog.avatarChanged()) {
        const QString path = dialog.selectedAvatarPath();
        QSettings().setValue(avatarSettingKey(), path);
        ui->profilePage->setAvatarPath(path);
        if (nickChanged)
            m_pendingNickname = nick;
        m_api->uploadAvatar(path);
        return;
    }
    if (nickChanged) {
        m_api->updateNickname(nick);
        return;
    }
    if (dialog.avatarChanged())
        ui->profilePage->setStatus(QStringLiteral("资料已保存"), true);
}

void MainWindow::onRecharge()
{
    RechargeDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    ui->profilePage->setStatus(QString());
    m_api->recharge(dialog.amountYuan());
}

void MainWindow::onRechargeRecords()
{
    RecordListDialog dialog(this);
    dialog.setHeading(QStringLiteral("充值记录"));
    connect(m_api, &ApiClient::rechargeRecordsReady, &dialog, &RecordListDialog::showRechargeRecords);
    connect(m_api, &ApiClient::apiFailed, &dialog,
            [&dialog](int, const QString &message) { dialog.showHint(message); });
    m_api->fetchRechargeRecords();
    dialog.exec();
}

void MainWindow::onOrderRecords()
{
    RecordListDialog dialog(this);
    dialog.setHeading(QStringLiteral("订单记录"));
    connect(m_api, &ApiClient::ordersReady, &dialog, &RecordListDialog::showOrders);
    connect(m_api, &ApiClient::apiFailed, &dialog,
            [&dialog](int, const QString &message) { dialog.showHint(message); });
    connect(&dialog, &RecordListDialog::orderClicked, this, [this, &dialog](const ChargingOrder &order) {
        openOrderDialog(order, &dialog);
    });
    m_api->fetchOrders();
    dialog.exec();
}

void MainWindow::onActiveOrderClicked()
{
    if (m_hasActiveOrder) {
        openOrderDialog(m_activeOrder);
        return;
    }
    m_openActiveWhenReady = true;
    m_api->fetchActiveOrder();
}

void MainWindow::onLogout()
{
    m_api->clearSession();
    m_user = User{};
    m_activeOrder = ChargingOrder{};
    m_createdOrder = ChargingOrder{};
    m_hasActiveOrder = false;
    m_autoReserve = false;
    m_openActiveWhenReady = false;
    m_pendingNickname.clear();
    ui->homePage->setActiveOrder(false, ChargingOrder{});
    ui->profilePage->setActiveOrder(false, ChargingOrder{});
    ui->rootStack->setCurrentIndex(0);
    ui->contentStack->setCurrentIndex(0);
    ui->tabHome->setChecked(true);
    ui->tabMine->setChecked(false);
    ui->loginPage->setStatus(QString());
}

void MainWindow::onApiFailed(int code, const QString &message)
{
    if (m_orderDialog)
        m_orderDialog->setHint(message);

    if (ui->rootStack->currentIndex() == 0) {
        ui->loginPage->setStatus(message);
    } else {
        ui->homePage->showHint(message);
        ui->profilePage->setStatus(message);
    }

    if (code == 40001) {
        m_autoReserve = false;
        warnUnfinishedOrder(m_stationDialog ? static_cast<QWidget *>(m_stationDialog.data()) : this);
        if (m_hasActiveOrder) {
            openOrderDialog(m_activeOrder, m_stationDialog);
        } else {
            m_openActiveWhenReady = true;
            m_api->fetchActiveOrder();
        }
    } else if (code == 40005 && m_orderDialog) {
        m_orderDialog->setHint(message);
    } else if (m_createdOrder.id > 0 && m_createdOrder.status == QLatin1String("PENDING")
               && !m_orderDialog && m_stationDialog) {
        openOrderDialog(m_createdOrder, m_stationDialog);
    }

    if (code == 20001)
        onLogout();
}

void MainWindow::setBusy(bool busy)
{
    m_inflight = busy ? m_inflight + 1 : qMax(0, m_inflight - 1);
    const bool on = m_inflight > 0;
    ui->loginPage->setBusy(on);
    ui->homePage->setBusy(on);
    ui->profilePage->setBusy(on);
}

void MainWindow::showAppPage()
{
    ui->rootStack->setCurrentIndex(1);
    ui->contentStack->setCurrentIndex(0);
    ui->tabHome->setChecked(true);
    ui->tabMine->setChecked(false);
}

void MainWindow::applyUser(const User &user)
{
    m_user = user;
    ui->profilePage->setUser(user);
    const QString localAvatar = QSettings().value(avatarSettingKey()).toString();
    if (!localAvatar.isEmpty())
        ui->profilePage->setAvatarPath(localAvatar);
    else if (!user.avatarUrl.isEmpty())
        ui->profilePage->setAvatarPath(user.avatarUrl);
}

void MainWindow::applyActiveOrder(bool hasOrder, const ChargingOrder &order)
{
    m_hasActiveOrder = hasOrder && isActiveOrderStatus(order.status);
    m_activeOrder = m_hasActiveOrder ? order : ChargingOrder{};
    ui->homePage->setActiveOrder(m_hasActiveOrder, m_activeOrder);
    ui->profilePage->setActiveOrder(m_hasActiveOrder, m_activeOrder);
}

void MainWindow::warnUnfinishedOrder(QWidget *parent)
{
    QDialog dialog(parent ? parent : this);
    dialog.setWindowTitle(QStringLiteral("提示"));
    dialog.setModal(true);
    dialog.setMinimumWidth(300);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 24, 20, 24);
    auto *label = new QLabel(QStringLiteral("您有未完成的充电订单，请先结算"));
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral(
        "color:#dc2626;font-size:16px;font-weight:700;padding:8px 4px;"));
    layout->addWidget(label);
    dialog.exec();
}

void MainWindow::openOrderDialog(const ChargingOrder &order, QWidget *parent)
{
    if (m_orderDialog) {
        m_orderDialog->setOrder(order);
        m_orderDialog->setBalanceCents(m_user.balanceCents);
        m_orderDialog->raise();
        m_orderDialog->activateWindow();
        return;
    }

    OrderDialog dialog(parent ? parent : this);
    m_orderDialog = &dialog;
    dialog.setDemoMode(m_api->demoMode());
    dialog.setBalanceCents(m_user.balanceCents);
    dialog.setOrder(order);

    connect(&dialog, &OrderDialog::reserveRequested, m_api, &ApiClient::reserveOrder);
    connect(&dialog, &OrderDialog::startRequested, m_api, &ApiClient::startOrder);
    connect(&dialog, &OrderDialog::stopRequested, m_api, &ApiClient::stopOrder);
    connect(&dialog, &OrderDialog::settleRequested, m_api, &ApiClient::settleOrder);
    connect(&dialog, &OrderDialog::cancelRequested, m_api, &ApiClient::cancelOrder);
    connect(&dialog, &OrderDialog::expireDemoRequested, m_api, &ApiClient::expireReservationForDemo);
    connect(&dialog, &OrderDialog::refreshRequested, m_api, &ApiClient::fetchOrderDetail);
    connect(&dialog, &OrderDialog::rechargeRequested, this, [this]() {
        onRecharge();
        if (m_orderDialog)
            m_orderDialog->setBalanceCents(m_user.balanceCents);
    });

    if (order.id > 0)
        m_api->fetchOrderDetail(order.id);
    dialog.exec();
    m_orderDialog.clear();
    m_api->fetchActiveOrder();
}

void MainWindow::refreshStationPiles()
{
    if (m_stationDialog)
        m_api->fetchStationPiles(m_stationDialog->station().id);
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
        #brandLabel { color: #0f766e; font-size: 13px; font-weight: 700; letter-spacing: 1px; }
        #titleLabel { color: #134e4a; font-size: 24px; font-weight: 700; }
        #sectionLabel { color: #134e4a; font-size: 15px; font-weight: 700; }
        #subtitleLabel, #hintLabel, #cardInfo { color: #5b6f69; font-size: 13px; }
        #statusLabel { color: #dc2626; font-size: 13px; }
        #successLabel { color: #059669; font-size: 13px; font-weight: 600; }
        #cardTitle { color: #134e4a; font-size: 16px; font-weight: 600; }
        #pileCard {
            background: #ffffff;
            border: 1px solid #d7ebe4;
            border-radius: 12px;
        }
        #pileNo { color: #134e4a; font-size: 14px; font-weight: 700; }
        #pileStatus_IDLE { color: #0d9488; font-weight: 700; }
        #pileStatus_RESERVED { color: #d97706; font-weight: 700; }
        #pileStatus_CHARGING { color: #2563eb; font-weight: 700; }
        #pileStatus_FAULT { color: #dc2626; font-weight: 700; }
        #pileStatus_OFFLINE { color: #78716c; font-weight: 700; }
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
        QCheckBox, QRadioButton { color: #3f5c55; spacing: 8px; }
        QPushButton#secondaryButton {
            background: #ffffff;
            color: #0f766e;
            border: 1px solid #99d6cc;
        }
        QPushButton#secondaryButton:hover { background: #ccfbf1; }
        QPushButton#navInlineButton {
            background: transparent;
            color: #0d9488;
            border: none;
            padding: 2px 0 2px 6px;
            font-weight: 700;
            min-width: 0;
        }
        QPushButton#navInlineButton:hover { color: #0f766e; background: #ccfbf1; }
        QPushButton#editCornerButton {
            background: #ffffff;
            color: #0f766e;
            border: 1px solid #99d6cc;
            padding: 6px 14px;
            min-width: 56px;
        }
        QPushButton#editCornerButton:hover { background: #ccfbf1; }
        QPushButton#rechargeInlineButton {
            padding: 6px 14px;
            min-width: 64px;
        }
        QPushButton#menuRow {
            background: #ffffff;
            color: #134e4a;
            border: 1px solid #d7ebe4;
            text-align: left;
            padding: 12px 14px;
        }
        QPushButton#menuRow:hover { background: #f0fdfa; }
        QPushButton#modeTab {
            background: #ffffff;
            color: #0f766e;
            border: 1px solid #99d6cc;
            padding: 8px;
            font-weight: 700;
        }
        QPushButton#modeTab:hover { background: #ccfbf1; }
        QPushButton#modeTab:checked {
            background: #0d9488;
            color: #ffffff;
            border: 1px solid #0d9488;
        }
        QPushButton#amountChip {
            background: #ffffff;
            color: #0f766e;
            border: 1px solid #99d6cc;
            padding: 8px 0;
            min-width: 0;
            font-weight: 700;
        }
        QPushButton#amountChip:hover { background: #ccfbf1; }
        QPushButton#amountChip:checked {
            background: #0d9488;
            color: #ffffff;
            border: 1px solid #0d9488;
        }
        QToolButton#amountStepButton {
            background: #0d9488;
            color: #ffffff;
            border: none;
            border-radius: 10px;
            min-width: 40px;
            max-width: 40px;
            min-height: 40px;
            font-size: 20px;
            font-weight: 700;
            padding: 0;
        }
        QToolButton#amountStepButton:hover { background: #0f766e; }
        QToolButton#amountStepButton:disabled { background: #cbd5e1; color: #ffffff; }
        QScrollArea { background: transparent; border: none; }
        QDialog { background: #f4f7f2; }
    )"));
}
