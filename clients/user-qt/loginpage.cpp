// 实现手机号格式校验、登录入口和服务地址/状态展示。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "loginpage.h"
#include "ui_loginpage.h"

#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

/// 创建手机号登录表单、后端地址输入和登录状态控件。
LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginPage)
{
    ui->setupUi(this);
    ui->demoCheck->hide();
    ui->brandLabel->setObjectName(QStringLiteral("brandLabel"));
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->subtitleLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->statusLabel->setObjectName(QStringLiteral("statusLabel"));
    ui->apiHintLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->hintLabel->setObjectName(QStringLiteral("hintLabel"));
    ui->loginLayout->setContentsMargins(24, 34, 24, 24);
    ui->loginLayout->setSpacing(14);
    ui->brandLabel->setText(QStringLiteral("GRID NODE / BEIJING / 08"));
    ui->titleLabel->setText(QStringLiteral("接入充电网络"));
    ui->subtitleLabel->setText(QStringLiteral("手机号即是通行证，新号码将自动建立账户。"));
    ui->apiHintLabel->setText(QStringLiteral("服务节点 / SOCKET ENDPOINT"));
    ui->hintLabel->setText(QStringLiteral("演示账号  13900000000\n冻结测试  13800000000"));
    ui->phoneEdit->setPlaceholderText(QStringLiteral("11 位手机号"));
    ui->loginButton->setText(QStringLiteral("连接并登录"));

    auto *phoneLabel = new QLabel(QStringLiteral("手机号 / MOBILE"), this);
    phoneLabel->setObjectName(QStringLiteral("cardTitle"));
    ui->loginLayout->insertWidget(3, phoneLabel);
    ui->phoneEdit->setAccessibleName(QStringLiteral("手机号"));
    ui->phoneEdit->setInputMethodHints(Qt::ImhDigitsOnly);
    ui->apiEdit->setAccessibleName(QStringLiteral("Qt 后端地址"));

    ui->phoneEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(R"(1[3-9]\d{9})")), ui->phoneEdit));

    connect(ui->loginButton, &QPushButton::clicked, this, &LoginPage::loginClicked);
    connect(ui->phoneEdit, &QLineEdit::returnPressed, this, &LoginPage::loginClicked);
}

LoginPage::~LoginPage()
{
    delete ui;
}

/// 返回去除首尾空格的手机号输入。
QString LoginPage::phone() const
{
    return ui->phoneEdit->text().trimmed();
}

/// 返回用户输入的 Qt Socket 后端地址。
QString LoginPage::apiBaseUrl() const
{
    return ui->apiEdit->text().trimmed();
}

/// 返回当前客户端的演示模式状态。
bool LoginPage::demoMode() const
{
    return false;
}

/// 维护请求计数器并统一切换各页面的忙碌状态。
void LoginPage::setBusy(bool busy)
{
    ui->loginButton->setEnabled(!busy);
    ui->loginButton->setText(busy ? QStringLiteral("正在建立连接…") : QStringLiteral("连接并登录"));
}

/// 更新登录页状态文字。
void LoginPage::setStatus(const QString &text)
{
    ui->statusLabel->setText(text);
}
