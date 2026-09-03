#include "profilepage.h"
#include "ui_profilepage.h"

#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>

ProfilePage::ProfilePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ProfilePage)
{
    ui->setupUi(this);
    ui->nicknameLabel->setObjectName(QStringLiteral("cardTitle"));
    ui->uidLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->balanceLabel->setObjectName(QStringLiteral("cardTitle"));
    ui->statusLabel->setObjectName(QStringLiteral("statusLabel"));
    ui->editButton->setObjectName(QStringLiteral("editCornerButton"));
    ui->rechargeButton->setObjectName(QStringLiteral("rechargeInlineButton"));
    ui->rechargeRecordsButton->setObjectName(QStringLiteral("menuRow"));
    ui->orderRecordsButton->setObjectName(QStringLiteral("menuRow"));
    ui->logoutButton->setObjectName(QStringLiteral("dangerButton"));
    ui->avatarLabel->setObjectName(QStringLiteral("avatar"));
    ui->rechargeRecordsButton->setStyleSheet(QStringLiteral("text-align: left;"));
    ui->orderRecordsButton->setStyleSheet(QStringLiteral("text-align: left;"));

    connect(ui->editButton, &QPushButton::clicked, this, &ProfilePage::editClicked);
    connect(ui->rechargeButton, &QPushButton::clicked, this, &ProfilePage::rechargeClicked);
    connect(ui->rechargeRecordsButton, &QPushButton::clicked, this, &ProfilePage::rechargeRecordsClicked);
    connect(ui->orderRecordsButton, &QPushButton::clicked, this, &ProfilePage::orderRecordsClicked);
    connect(ui->logoutButton, &QPushButton::clicked, this, &ProfilePage::logoutClicked);
}

ProfilePage::~ProfilePage()
{
    delete ui;
}

void ProfilePage::setUser(const User &user)
{
    ui->nicknameLabel->setText(user.nickname.isEmpty()
                                   ? QStringLiteral("用户")
                                   : user.nickname);
    const QString uid = user.phone.size() >= 4 ? user.phone.right(4) : user.phone;
    ui->uidLabel->setText(QStringLiteral("UID  %1").arg(uid));
    ui->balanceLabel->setText(QStringLiteral("钱包余额  ¥ %1").arg(centsToYuanText(user.balanceCents)));
    if (user.avatarUrl.isEmpty()) {
        ui->avatarLabel->clear();
        ui->avatarLabel->setText(QStringLiteral("默认"));
        ui->avatarLabel->setStyleSheet(QStringLiteral("background:#64748b;border-radius:36px;color:white;"));
    }
}

void ProfilePage::setAvatarPath(const QString &path)
{
    QPixmap pix(path);
    if (pix.isNull())
        return;
    ui->avatarLabel->setText(QString());
    ui->avatarLabel->setPixmap(pix.scaled(72, 72, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    ui->avatarLabel->setStyleSheet(QStringLiteral("border-radius:36px;"));
}

void ProfilePage::setBusy(bool busy)
{
    setEnabled(!busy);
}

void ProfilePage::setStatus(const QString &text, bool success)
{
    ui->statusLabel->setText(text);
    ui->statusLabel->setObjectName(success
                                       ? QStringLiteral("successLabel")
                                       : QStringLiteral("statusLabel"));
    ui->statusLabel->style()->unpolish(ui->statusLabel);
    ui->statusLabel->style()->polish(ui->statusLabel);
    ui->statusLabel->update();
}
