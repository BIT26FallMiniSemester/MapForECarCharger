#include "loginwindow.h"
#include "ui_loginwindow.h"
#include "mainwindow.h"
#include "apiclient.h"
#include <QMessageBox>
#include <QSettings>
#include <QVBoxLayout>
#include <QUrl>

LoginWindow::LoginWindow(QWidget *parent) : QWidget(parent), ui(new Ui::LoginWindow), m_api(new ApiClient(this))
{
    ui->setupUi(this);
    setWindowTitle("管理员登录 - 充电林运营管理平台");
    connect(ui->loginButton, &QPushButton::clicked, this, &LoginWindow::login);
    connect(ui->passwordEdit, &QLineEdit::returnPressed, this, &LoginWindow::login);
    QSettings settings;
    m_serverEdit = new QLineEdit(settings.value("server/endpoint", "127.0.0.1:9000").toString());
    m_serverEdit->setPlaceholderText("Qt 后端地址，例如 127.0.0.1:9000");
    auto *layout = qobject_cast<QVBoxLayout *>(ui->loginPanel->layout());
    layout->insertWidget(5, m_serverEdit);
    connect(m_api, &ApiClient::succeeded, this, [this](const QString &path, const QJsonValue &data, const QJsonObject &) {
        if (path != "/admin/login" || !data.isObject()) return;
        const QString token = data.toObject().value("access_token").toString();
        if (token.isEmpty()) { ui->errorLabel->setText("登录响应缺少 access_token"); ui->loginButton->setEnabled(true); return; }
        openMainWindow(false, token);
    });
    connect(m_api, &ApiClient::failed, this, [this](const QString &path, int, int, const QString &message) {
        if (path != "/admin/login") return;
        ui->errorLabel->setText(message);
        ui->loginButton->setEnabled(true);
    });
}
LoginWindow::~LoginWindow() { delete ui; }
void LoginWindow::login()
{
    ui->errorLabel->clear();
    const QString username = ui->accountEdit->text().trimmed();
    const QString password = ui->passwordEdit->text();
    QSettings settings;
    settings.setValue("server/endpoint", m_serverEdit->text().trimmed());
    if (username.isEmpty() || password.isEmpty()) { ui->errorLabel->setText("请输入管理员账号和密码"); return; }
    QString endpoint = m_serverEdit->text().trimmed();
    if (!endpoint.contains("://")) endpoint.prepend("tcp://");
    const QUrl serverUrl(endpoint);
    if (!serverUrl.isValid() || serverUrl.host().isEmpty() || serverUrl.port(9000) < 1) { ui->errorLabel->setText("Qt 后端地址格式不正确，应为主机:端口"); return; }
    m_api->setBaseUrl(m_serverEdit->text());
    ui->loginButton->setEnabled(false);
    ui->errorLabel->setText("正在连接 Qt 后端……");
    m_api->post("/admin/login", {{"username", username}, {"password", password}});
}

void LoginWindow::openMainWindow(bool demoMode, const QString &token)
{
    QSettings settings;
    settings.setValue("admin/token", token);
    settings.setValue("server/endpoint", m_serverEdit->text().trimmed());
    auto *main = new MainWindow(demoMode, m_serverEdit->text().trimmed(), token);
    main->setAttribute(Qt::WA_DeleteOnClose);
    main->show();
    hide();
    connect(main, &QObject::destroyed, this, &QWidget::close);
}
