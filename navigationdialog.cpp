#include "navigationdialog.h"
#include "ui_navigationdialog.h"

#include "mapconfig.h"
#include "tencentgeocoder.h"
#include "webenginesetup.h"

#include <QDesktopServices>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QUrl>
#include <QUrlQuery>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

NavigationDialog::NavigationDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::NavigationDialog)
    , m_geo(new TencentGeocoder(this))
{
    ui->setupUi(this);
    resize(380, 560);

    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->originHint->setObjectName(QStringLiteral("sectionLabel"));
    ui->destHint->setObjectName(QStringLiteral("sectionLabel"));
    ui->destValue->setObjectName(QStringLiteral("cardInfo"));
    ui->modeHint->setObjectName(QStringLiteral("subtitleLabel"));
    ui->originStatus->setObjectName(QStringLiteral("subtitleLabel"));
    ui->mapHint->setObjectName(QStringLiteral("subtitleLabel"));
    ui->closeButton->setObjectName(QStringLiteral("secondaryButton"));
    ui->searchOriginButton->setObjectName(QStringLiteral("secondaryButton"));
    ui->navigateButton->setCursor(Qt::PointingHandCursor);
    ui->closeButton->setCursor(Qt::PointingHandCursor);
    ui->searchOriginButton->setCursor(Qt::PointingHandCursor);

    m_geo->setApiKey(tencentMapKey());

    ui->modeCombo->clear();
    ui->modeCombo->addItem(QStringLiteral("驾车"), QStringLiteral("drive"));
    ui->modeCombo->addItem(QStringLiteral("步行"), QStringLiteral("walk"));

    setupMapView();

    ui->currentOriginRadio->setChecked(true);
    updateOriginEnabled();

    connect(ui->currentOriginRadio, &QRadioButton::toggled, this, &NavigationDialog::updateOriginEnabled);
    connect(ui->customOriginRadio, &QRadioButton::toggled, this, &NavigationDialog::updateOriginEnabled);
    connect(ui->searchOriginButton, &QPushButton::clicked, this, &NavigationDialog::onSearchOrigin);
    connect(ui->originEdit, &QLineEdit::returnPressed, this, &NavigationDialog::onSearchOrigin);
    connect(ui->originEdit, &QLineEdit::textEdited, this, [this]() {
        m_hasCustom = false;
    });
    connect(ui->navigateButton, &QPushButton::clicked, this, &NavigationDialog::onNavigateClicked);
    connect(ui->closeButton, &QPushButton::clicked, this, &NavigationDialog::reject);
    connect(m_geo, &TencentGeocoder::geocodeSucceeded, this, &NavigationDialog::onGeocodeOk);
    connect(m_geo, &TencentGeocoder::geocodeFailed, this, &NavigationDialog::onGeocodeFail);
}

void NavigationDialog::setupMapView()
{
    if (!shouldEmbedWebEngine()) {
        ui->mapHost->hide();
        ui->mapHint->clear();
        return;
    }

    m_web = new QWebEngineView(ui->mapHost);
    m_web->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    m_web->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    connect(m_web->page(), &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus, int) {
                if (!m_waitingRoute)
                    return;
                m_waitingRoute = false;
                ui->mapHint->setText(QStringLiteral("内嵌地图进程退出，已用系统浏览器打开。"));
                if (m_lastRouteUrl.isValid())
                    QDesktopServices::openUrl(m_lastRouteUrl);
            });
    connect(m_web, &QWebEngineView::loadFinished, this, &NavigationDialog::onMapLoadFinished);
    ui->mapHostLayout->addWidget(m_web);
    ui->mapHint->setText(QStringLiteral("选择起点和出行方式后，点击「开始导航」。"));
}

NavigationDialog::~NavigationDialog()
{
    delete ui;
}

void NavigationDialog::setDestination(const StationSummary &station)
{
    m_toLat = station.latitude;
    m_toLng = station.longitude;
    m_toName = station.name;
    ui->destValue->setText(station.name + QStringLiteral("\n") + station.address);
}

void NavigationDialog::setOrigin(double latitude, double longitude, const QString &displayName)
{
    m_currentLat = latitude;
    m_currentLng = longitude;
    m_currentName = displayName;
    ui->currentOriginRadio->setText(QStringLiteral("当前位置（%1）").arg(displayName));
}

void NavigationDialog::updateOriginEnabled()
{
    const bool custom = ui->customOriginRadio->isChecked();
    ui->originEdit->setVisible(custom);
    ui->searchOriginButton->setVisible(custom);
    ui->originStatus->setVisible(custom && !ui->originStatus->text().isEmpty());
    if (!custom)
        ui->originStatus->clear();
}

void NavigationDialog::onSearchOrigin()
{
    m_pendingNavigate = false;
    ui->originStatus->setText(QStringLiteral("正在搜索起点…"));
    ui->originStatus->setVisible(true);
    m_geo->searchPlace(ui->originEdit->text());
}

void NavigationDialog::onNavigateClicked()
{
    if (m_toLat == 0.0 && m_toLng == 0.0) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("终点坐标无效，无法导航。"));
        return;
    }

    if (ui->currentOriginRadio->isChecked()) {
        if (m_currentLat == 0.0 && m_currentLng == 0.0) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("当前位置未知，请改用自己输入的起点。"));
            return;
        }
        loadRoute(m_currentLat, m_currentLng, m_currentName);
        return;
    }

    if (ui->originEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请输入起点地址。"));
        return;
    }
    if (m_hasCustom) {
        loadRoute(m_customLat, m_customLng, m_customName);
        return;
    }

    m_pendingNavigate = true;
    ui->originStatus->setText(QStringLiteral("正在解析起点…"));
    ui->originStatus->setVisible(true);
    ui->navigateButton->setEnabled(false);
    m_geo->searchPlace(ui->originEdit->text());
}

void NavigationDialog::onGeocodeOk(double latitude, double longitude, const QString &name)
{
    m_customLat = latitude;
    m_customLng = longitude;
    m_customName = name;
    m_hasCustom = true;
    ui->originStatus->setText(QStringLiteral("起点：%1").arg(name));
    ui->originStatus->setVisible(true);
    ui->navigateButton->setEnabled(true);
    if (m_pendingNavigate) {
        m_pendingNavigate = false;
        loadRoute(m_customLat, m_customLng, m_customName);
    }
}

void NavigationDialog::onGeocodeFail(const QString &message)
{
    m_pendingNavigate = false;
    m_hasCustom = false;
    ui->navigateButton->setEnabled(true);
    ui->originStatus->setText(message);
    ui->originStatus->setVisible(true);
}

void NavigationDialog::onMapLoadFinished(bool ok)
{
    if (!m_waitingRoute)
        return;
    m_waitingRoute = false;
    if (ok) {
        ui->mapHint->setText(QStringLiteral("已打开腾讯地图路线规划。"));
        return;
    }
    ui->mapHint->setText(QStringLiteral("内嵌地图加载失败，已用系统浏览器打开。"));
    if (m_lastRouteUrl.isValid())
        QDesktopServices::openUrl(m_lastRouteUrl);
}

void NavigationDialog::loadRoute(double fromLat, double fromLng, const QString &fromName)
{
    const QString type = ui->modeCombo->currentData().toString();
    QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), type);
    query.addQueryItem(QStringLiteral("from"), fromName);
    query.addQueryItem(QStringLiteral("fromcoord"),
                       QStringLiteral("%1,%2").arg(fromLat, 0, 'f', 6).arg(fromLng, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("to"), m_toName);
    query.addQueryItem(QStringLiteral("tocoord"),
                       QStringLiteral("%1,%2").arg(m_toLat, 0, 'f', 6).arg(m_toLng, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("policy"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("referer"), tencentMapKey());
    url.setQuery(query);
    m_lastRouteUrl = url;

    ui->mapHint->setText(QStringLiteral("正在打开腾讯地图路线规划…"));
    if (!m_web) {
        QDesktopServices::openUrl(url);
        return;
    }
    m_waitingRoute = true;
    m_web->load(url);
}
