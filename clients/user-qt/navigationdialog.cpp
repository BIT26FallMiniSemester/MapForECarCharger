#include "navigationdialog.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#ifdef HAVE_QT_WEBENGINE
#include <QWebEngineView>
#endif

NavigationDialog::NavigationDialog(double fromLatitude, double fromLongitude,
                                   const StationSummary &station, QWidget *parent)
    : QDialog(parent), m_fromLatitude(fromLatitude), m_fromLongitude(fromLongitude),
      m_station(station)
{
    setWindowTitle(QStringLiteral("路线节点 / %1").arg(station.name));
    setModal(true);
    resize(parent ? qMin(386, parent->width() - 24) : 386,
           parent ? qMin(700, parent->height() - 48) : 700);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto *eyebrow = new QLabel(QStringLiteral("ROUTE LINK / TENCENT MAP"));
    eyebrow->setObjectName(QStringLiteral("brandLabel"));
    layout->addWidget(eyebrow);
    auto *destination = new QLabel(QStringLiteral("目的节点\n%1\n%2").arg(station.name, station.address));
    destination->setObjectName(QStringLiteral("cardTitle"));
    destination->setWordWrap(true);
    layout->addWidget(destination);

    auto *bar = new QFormLayout;
    m_mode = new QComboBox;
    m_mode->addItem(QStringLiteral("驾车"), QStringLiteral("drive"));
    m_mode->addItem(QStringLiteral("步行"), QStringLiteral("walk"));
    bar->addRow(QStringLiteral("出行方式"), m_mode);
    layout->addLayout(bar);

    m_viewLayout = new QVBoxLayout;
    layout->addLayout(m_viewLayout, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    auto *navigate = buttons->addButton(QStringLiteral("在腾讯地图中导航"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(navigate, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(routeUrl()); });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] { loadRoute(); });
    loadRoute();
}

QUrl NavigationDialog::routeUrl() const
{
    QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), m_mode->currentData().toString());
    query.addQueryItem(QStringLiteral("from"), QStringLiteral("我的位置"));
    query.addQueryItem(QStringLiteral("fromcoord"), QStringLiteral("%1,%2")
        .arg(m_fromLatitude, 0, 'f', 6).arg(m_fromLongitude, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("to"), m_station.name);
    query.addQueryItem(QStringLiteral("tocoord"), QStringLiteral("%1,%2")
        .arg(m_station.latitude, 0, 'f', 6).arg(m_station.longitude, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("policy"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("referer"), QStringLiteral("MapForECarCharger"));
    url.setQuery(query);
    return url;
}

void NavigationDialog::loadRoute()
{
    while (QLayoutItem *item = m_viewLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
#ifdef HAVE_QT_WEBENGINE
    auto *view = new QWebEngineView(this);
    view->setMinimumHeight(360);
    view->setAccessibleName(QStringLiteral("腾讯地图导航路线"));
    view->setZoomFactor(0.85);
    view->setUrl(routeUrl());
    m_viewLayout->addWidget(view);
#else
    auto *hint = new QLabel(QStringLiteral(
        "当前开发机未安装 Qt WebEngine。点击下方“在腾讯地图中导航”可查看完整路线；"
        "安装 qt6-webengine-dev 后将直接在此窗口显示腾讯地图。"), this);
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    m_viewLayout->addWidget(hint);
#endif
}
