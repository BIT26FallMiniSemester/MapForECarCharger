// 实现用户资料展示、头像预览、充值金额读取和充值记录渲染。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "profilepage.h"
#include "ui_profilepage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>

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

    connect(ui->saveNicknameButton, &QPushButton::clicked, this, &ProfilePage::saveNicknameClicked);
    connect(ui->pickAvatarButton, &QPushButton::clicked, this, &ProfilePage::chooseAvatarClicked);
    connect(ui->rechargeButton, &QPushButton::clicked, this, &ProfilePage::rechargeClicked);
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
