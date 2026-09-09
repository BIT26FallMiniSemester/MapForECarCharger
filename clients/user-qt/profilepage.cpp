// 实现用户资料展示、头像预览、充值金额读取和充值记录渲染。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "profilepage.h"
#include "ui_profilepage.h"

#include <QDateTime>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QPixmap roundAvatar(const QPixmap &source)
{
    QPixmap result(72, 72);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(result.rect());
    painter.setClipPath(path);
    const QPixmap scaled = source.scaled(72, 72, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
    painter.drawPixmap(0, 0, scaled.copy((scaled.width() - 72) / 2,
                                         (scaled.height() - 72) / 2, 72, 72));
    return result;
}

QString orderStatusText(const QString &status)
{
    if (status == QStringLiteral("PENDING")) return QStringLiteral("待预约");
    if (status == QStringLiteral("RESERVED")) return QStringLiteral("已预约");
    if (status == QStringLiteral("CHARGING")) return QStringLiteral("充电中");
    if (status == QStringLiteral("UNPAID")) return QStringLiteral("待支付");
    if (status == QStringLiteral("COMPLETED")) return QStringLiteral("已完成");
    if (status == QStringLiteral("CANCELLED")) return QStringLiteral("已取消");
    return status;
}

QString localOrderTime(const QString &value)
{
    QDateTime time = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!time.isValid()) time = QDateTime::fromString(value, Qt::ISODate);
    return time.isValid() ? time.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : value;
}
}

/// 创建个人中心表单、余额、头像、充值记录和操作按钮。
ProfilePage::ProfilePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ProfilePage)
{
    ui->setupUi(this);
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->titleLabel->setText(QStringLiteral("账户与能量"));
    ui->phoneLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->balanceLabel->setObjectName(QStringLiteral("cardTitle"));
    ui->rechargeTitleLabel->setObjectName(QStringLiteral("cardTitle"));
    ui->rechargeTitleLabel->setText(QStringLiteral("充值余额 / WALLET"));
    ui->recordTitleLabel->setObjectName(QStringLiteral("cardTitle"));
    ui->recordTitleLabel->setText(QStringLiteral("资金记录 / LEDGER"));
    ui->statusLabel->setObjectName(QStringLiteral("statusLabel"));
    ui->pickAvatarButton->setObjectName(QStringLiteral("secondaryButton"));
    ui->logoutButton->setObjectName(QStringLiteral("dangerButton"));
    ui->avatarLabel->setObjectName(QStringLiteral("avatar"));

    ui->profileLayout->setContentsMargins(16, 16, 16, 12);
    ui->profileLayout->setSpacing(8);
    ui->profileLayout->removeWidget(ui->avatarLabel);
    ui->profileLayout->removeWidget(ui->pickAvatarButton);
    ui->profileLayout->removeWidget(ui->nicknameEdit);
    ui->profileLayout->removeWidget(ui->saveNicknameButton);
    ui->profileLayout->removeWidget(ui->rechargeSpin);
    ui->profileLayout->removeWidget(ui->rechargeButton);

    auto *avatarRow = new QHBoxLayout;
    avatarRow->setSpacing(12);
    avatarRow->addWidget(ui->avatarLabel);
    avatarRow->addWidget(ui->pickAvatarButton, 1);
    ui->profileLayout->insertLayout(1, avatarRow);

    auto *nicknameTitle = new QLabel(QStringLiteral("昵称"));
    nicknameTitle->setObjectName(QStringLiteral("cardTitle"));
    auto *nicknameRow = new QHBoxLayout;
    nicknameRow->setSpacing(8);
    nicknameRow->addWidget(ui->nicknameEdit, 1);
    nicknameRow->addWidget(ui->saveNicknameButton);
    ui->profileLayout->insertWidget(4, nicknameTitle);
    ui->profileLayout->insertLayout(5, nicknameRow);

    auto *rechargeRow = new QHBoxLayout;
    rechargeRow->setSpacing(8);
    rechargeRow->addWidget(ui->rechargeSpin, 1);
    rechargeRow->addWidget(ui->rechargeButton);
    ui->profileLayout->insertLayout(7, rechargeRow);
    ui->recordScroll->setMinimumHeight(96);

    auto *eyebrow = new QLabel(QStringLiteral("DRIVER ID / ACCOUNT 03"), this);
    eyebrow->setObjectName(QStringLiteral("brandLabel"));
    ui->profileLayout->insertWidget(0, eyebrow);
    ui->pickAvatarButton->setText(QStringLiteral("更换识别图像"));
    ui->saveNicknameButton->setText(QStringLiteral("更新昵称"));
    ui->rechargeButton->setText(QStringLiteral("充值"));
    ui->logoutButton->setText(QStringLiteral("断开账户"));

    m_historyButton = new QPushButton(QStringLiteral("查看全部历史订单  →"), this);
    m_historyButton->setObjectName(QStringLiteral("secondaryButton"));
    m_historyButton->setCursor(Qt::PointingHandCursor);
    m_historyButton->setAccessibleName(QStringLiteral("查看全部历史订单"));
    ui->profileLayout->insertWidget(ui->profileLayout->count() - 2, m_historyButton);

    connect(ui->saveNicknameButton, &QPushButton::clicked, this, &ProfilePage::saveNicknameClicked);
    connect(ui->pickAvatarButton, &QPushButton::clicked, this, &ProfilePage::chooseAvatarClicked);
    connect(ui->rechargeButton, &QPushButton::clicked, this, &ProfilePage::rechargeClicked);
    connect(m_historyButton, &QPushButton::clicked, this, [this] {
        m_historyButton->setEnabled(false);
        m_historyButton->setText(QStringLiteral("正在读取订单档案…"));
        emit orderHistoryClicked();
    });
    connect(ui->logoutButton, &QPushButton::clicked, this, &ProfilePage::logoutClicked);
}

ProfilePage::~ProfilePage()
{
    delete ui;
}

/// 返回清理后的昵称输入。
QString ProfilePage::nickname() const
{
    return ui->nicknameEdit->text().trimmed();
}

/// 返回用户输入的充值金额。
double ProfilePage::rechargeYuan() const
{
    return ui->rechargeSpin->value();
}

/// 把用户模型渲染到个人中心。
void ProfilePage::setUser(const User &user)
{
    ui->phoneLabel->setText(QStringLiteral("手机号  %1").arg(user.phone));
    ui->balanceLabel->setText(QStringLiteral("余额  ¥ %1").arg(centsToYuanText(user.balanceCents)));
    ui->nicknameEdit->setText(user.nickname);
    if (user.avatarUrl.isEmpty()) {
        ui->avatarLabel->clear();
        ui->avatarLabel->setText(QStringLiteral("默认"));
        ui->avatarLabel->setStyleSheet(QStringLiteral("background:#eaf8f3;border:2px solid #00a878;border-radius:36px;color:#008c68;font-weight:700;"));
    }
}

/// 从本地文件加载并缩放头像。
void ProfilePage::setAvatarPath(const QString &path)
{
    QPixmap pix(path);
    if (pix.isNull())
        return;
    ui->avatarLabel->setText(QString());
    ui->avatarLabel->setPixmap(roundAvatar(pix));
    ui->avatarLabel->setStyleSheet(QStringLiteral("background:transparent;"));
}

void ProfilePage::setAvatarData(const QByteArray &content)
{
    QPixmap pix;
    if (!pix.loadFromData(content))
        return;
    ui->avatarLabel->setText(QString());
    ui->avatarLabel->setPixmap(roundAvatar(pix));
    ui->avatarLabel->setStyleSheet(QStringLiteral("background:transparent;"));
}

/// 维护请求计数器并统一切换各页面的忙碌状态。
void ProfilePage::setBusy(bool busy)
{
    setEnabled(!busy);
}

/// 更新登录页状态文字。
void ProfilePage::setStatus(const QString &text)
{
    ui->statusLabel->setText(text);
}

/// 把充值记录渲染为列表。
void ProfilePage::showRechargeRecords(const QVector<RechargeRecord> &records)
{
    while (ui->recordListLayout->count() > 1) {
        QLayoutItem *item = ui->recordListLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    if (records.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("暂无充值记录"));
        empty->setObjectName(QStringLiteral("subtitleLabel"));
        ui->recordListLayout->insertWidget(0, empty);
        return;
    }
    for (const RechargeRecord &r : records) {
        auto *line = new QLabel(
            QStringLiteral("+¥%1    余额 ¥%2\n%3")
                .arg(centsToYuanText(r.amountCents))
                .arg(centsToYuanText(r.balanceAfterCents))
                .arg(r.createdAt));
        line->setObjectName(QStringLiteral("cardInfo"));
        line->setWordWrap(true);
        ui->recordListLayout->insertWidget(ui->recordListLayout->count() - 1, line);
    }
}

void ProfilePage::showOrderHistory(const QVector<ChargingOrder> &orders)
{
    m_historyButton->setEnabled(true);
    m_historyButton->setText(QStringLiteral("查看全部历史订单  %1 笔  →").arg(orders.size()));

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("历史订单"));
    dialog.resize(qBound(340, width() - 16, 390), qBound(500, height() - 24, 680));
    auto *root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 18, 16, 14);
    root->setSpacing(10);

    auto *eyebrow = new QLabel(QStringLiteral("ORDER ARCHIVE / ALL"), &dialog);
    eyebrow->setObjectName(QStringLiteral("brandLabel"));
    auto *title = new QLabel(QStringLiteral("历史充电订单"), &dialog);
    title->setObjectName(QStringLiteral("titleLabel"));
    auto *summary = new QLabel(QStringLiteral("共 %1 笔，按创建时间从新到旧排列").arg(orders.size()), &dialog);
    summary->setObjectName(QStringLiteral("subtitleLabel"));
    root->addWidget(eyebrow);
    root->addWidget(title);
    root->addWidget(summary);

    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *host = new QWidget(scroll);
    auto *list = new QVBoxLayout(host);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(8);

    if (orders.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("还没有历史订单。完成一次充电后，订单会记录在这里。"), host);
        empty->setObjectName(QStringLiteral("subtitleLabel"));
        empty->setWordWrap(true);
        list->addWidget(empty);
    }
    for (const ChargingOrder &order : orders) {
        auto *card = new QFrame(host);
        card->setObjectName(QStringLiteral("orderCard"));
        card->setProperty("orderState", order.status);
        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(12, 10, 12, 10);
        box->setSpacing(5);
        auto *header = new QHBoxLayout;
        auto *number = new QLabel(order.orderNo, card);
        number->setObjectName(QStringLiteral("orderNumber"));
        auto *status = new QLabel(orderStatusText(order.status), card);
        status->setObjectName(QStringLiteral("orderStatus"));
        status->setProperty("orderState", order.status);
        header->addWidget(number, 1);
        header->addWidget(status);
        auto *station = new QLabel(QStringLiteral("%1  ·  %2").arg(order.stationName, order.pileNo), card);
        station->setObjectName(QStringLiteral("cardTitle"));
        station->setWordWrap(true);
        const qint64 minutes = order.durationSeconds / 60;
        const qint64 seconds = order.durationSeconds % 60;
        auto *detail = new QLabel(
            QStringLiteral("%1\n电量 %2 kWh  ·  时长 %3:%4  ·  金额 ¥%5")
                .arg(localOrderTime(order.createdAt))
                .arg(order.energyWh / 1000.0, 0, 'f', 2)
                .arg(minutes)
                .arg(seconds, 2, 10, QLatin1Char('0'))
                .arg(centsToYuanText(order.amountCents)), card);
        detail->setObjectName(QStringLiteral("cardInfo"));
        detail->setWordWrap(true);
        box->addLayout(header);
        box->addWidget(station);
        box->addWidget(detail);
        list->addWidget(card);
    }
    list->addStretch();
    scroll->setWidget(host);
    root->addWidget(scroll, 1);

    auto *close = new QPushButton(QStringLiteral("返回账户"), &dialog);
    close->setObjectName(QStringLiteral("secondaryButton"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    root->addWidget(close);
    dialog.exec();
}

void ProfilePage::showOrderHistoryError(const QString &message)
{
    m_historyButton->setEnabled(true);
    m_historyButton->setText(QStringLiteral("重新加载历史订单  →"));
    setStatus(message);
}
