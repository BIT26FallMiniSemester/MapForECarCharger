#include "loginpage.h"
#include "ui_loginpage.h"

#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginPage)
{
    ui->setupUi(this);
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

QString LoginPage::phone() const
{
    return ui->phoneEdit->text().trimmed();
}

QString LoginPage::apiBaseUrl() const
{
    return ui->apiEdit->text().trimmed();
}

bool LoginPage::demoMode() const
{
    return ui->demoCheck->isChecked();
}

void LoginPage::setBusy(bool busy)
{
    ui->loginButton->setEnabled(!busy);
    ui->loginButton->setText(busy ? QStringLiteral("登录中...") : QStringLiteral("登录 / 自动注册"));
}

void LoginPage::setStatus(const QString &text)
{
    ui->statusLabel->setText(text);
}
