// 实现手机号格式校验、登录入口和服务地址/状态展示。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "loginpage.h"
#include "ui_loginpage.h"

#include <QLineEdit>
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
    ui->loginButton->setText(busy ? QStringLiteral("登录中...") : QStringLiteral("登录 / 自动注册"));
}

/// 更新登录页状态文字。
void LoginPage::setStatus(const QString &text)
{
    ui->statusLabel->setText(text);
}
