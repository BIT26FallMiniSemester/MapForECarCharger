#include "loginwindow.h"
#include "ui_loginwindow.h"
#include "mainwindow.h"
#include "apiclient.h"
#include <QCheckBox>
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
    m_serverEdit = new QLineEdit(settings.value("server/baseUrl", "http://127.0.0.1:8000/api/v1").toString());
    m_serverEdit->setPlaceholderText("后端地址，例如 http://127.0.0.1:8000/api/v1");
    m_demoCheck = new QCheckBox("使用内置演示数据（后端未启动时勾选）");
    m_demoCheck->setChecked(settings.value("server/demoMode", true).toBool());
    auto *layout = qobject_cast<QVBoxLayout *>(ui->loginPanel->layout());
    layout->insertWidget(5, m_serverEdit);
    layout->insertWidget(6, m_demoCheck);
    m_serverEdit->setEnabled(!m_demoCheck->isChecked());
    connect(m_demoCheck, &QCheckBox::toggled, m_serverEdit, &QWidget::setDisabled);
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
    settings.setValue("server/demoMode", m_demoCheck->isChecked());
    settings.setValue("server/baseUrl", m_serverEdit->text().trimmed());
    if (m_demoCheck->isChecked()) {
        if (username == "admin" && password == "123456") openMainWindow(true, "mock-admin-token-2026");
        else {
            ui->errorLabel->setText("账号或密码错误，请使用演示账号 admin / 123456");
            ui->passwordEdit->clear();
            ui->passwordEdit->setFocus();
        }
    } else {
        if (username.isEmpty() || password.isEmpty()) { ui->errorLabel->setText("请输入管理员账号和密码"); return; }
        const QUrl serverUrl(m_serverEdit->text().trimmed());
        if (!serverUrl.isValid() || serverUrl.scheme().isEmpty() || serverUrl.host().isEmpty()) { ui->errorLabel->setText("后端地址格式不正确，应以 http:// 或 https:// 开头"); return; }
        m_api->setBaseUrl(m_serverEdit->text());
        ui->loginButton->setEnabled(false);
        ui->errorLabel->setText("正在连接后端……");
        m_api->post("/admin/login", {{"username", username}, {"password", password}});
    }
}

void LoginWindow::openMainWindow(bool demoMode, const QString &token)
{
    QSettings settings;
    settings.setValue("admin/token", token);
    settings.setValue("server/demoMode", demoMode);
    settings.setValue("server/baseUrl", m_serverEdit->text().trimmed());
    auto *main = new MainWindow(demoMode, m_serverEdit->text().trimmed(), token);
    main->setAttribute(Qt::WA_DeleteOnClose);
    main->show();
    hide();
    connect(main, &QObject::destroyed, this, &QWidget::close);
}
