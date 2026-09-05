#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "mockrepository.h"
#include "apiclient.h"
#include <QtCharts>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QUrlQuery>
#include <numeric>

static QLabel *titleLabel(const QString &text) { auto *l = new QLabel(text); l->setObjectName("pageTitle"); return l; }
static void resetHost(QWidget *host) { if (auto *old = host->layout()) { QLayoutItem *i; while ((i = old->takeAt(0))) { delete i->widget(); delete i; } delete old; } }
static int selectedId(QTableWidget *t) { return t && t->currentRow() >= 0 ? t->item(t->currentRow(), 0)->data(Qt::UserRole).toInt() : -1; }
static void info(QWidget *p, const QString &title, const QString &text) { QMessageBox::information(p, title, text); }

MainWindow::MainWindow(bool demoMode, const QString &baseUrl, const QString &token, QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow), m_demoMode(demoMode), m_api(new ApiClient(this))
{
    ui->setupUi(this);
    setWindowTitle("充电林运营管理平台 - PC 管理端");
    resize(1360, 820);
    m_api->setBaseUrl(baseUrl.isEmpty() ? "http://127.0.0.1:8000/api/v1" : baseUrl);
    m_api->setToken(token);
    connect(m_api, &ApiClient::succeeded, this, &MainWindow::handleApiSuccess);
    connect(m_api, &ApiClient::failed, this, &MainWindow::handleApiFailure);
    ui->stackedWidget->addWidget(createDashboard());
    ui->stackedWidget->addWidget(createPilePage());
    ui->stackedWidget->addWidget(createStationPage());
    ui->stackedWidget->addWidget(createUserPage());
    connect(ui->navigationList, &QListWidget::currentRowChanged, ui->stackedWidget, &QStackedWidget::setCurrentIndex);
    ui->navigationList->setCurrentRow(0);
    ui->statusbar->showMessage(m_demoMode ? "内置演示数据模式" : "已连接后端 · " + m_api->baseUrl());
    for(QLabel *label:ui->sidebar->findChildren<QLabel*>())if(label->text().contains("演示数据模式"))label->setText(m_demoMode?"管理员：admin\n演示数据模式":"管理员：admin\n真实后端模式");
    if (!m_demoMode) {
        loadDashboard(7);
        refreshStations();
        refreshPiles();
        refreshUsers();
    }
}
MainWindow::~MainWindow() { delete ui; }

QWidget *MainWindow::metricCard(const QString &title, const QString &value, const QString &hint)
{
    auto *box = new QFrame; box->setObjectName("metricCard");
    auto *v = new QVBoxLayout(box);
    auto *t = new QLabel(title); t->setObjectName("metricTitle");
    auto *n = new QLabel(value); n->setObjectName("metricValue");
    auto *h = new QLabel(hint); h->setObjectName("metricHint");
    m_metricValues.append(n);
    v->addWidget(t); v->addWidget(n); v->addWidget(h); return box;
}

QWidget *MainWindow::createDashboard()
{
    auto *page = new QWidget; auto *v = new QVBoxLayout(page);
    auto *head = new QHBoxLayout; head->addWidget(titleLabel("运营首页")); head->addStretch();
    auto *period = new QComboBox; period->addItems({"近 7 日", "近 30 日"}); head->addWidget(new QLabel("趋势周期")); head->addWidget(period); v->addLayout(head);
    auto *cards = new QHBoxLayout;
    cards->addWidget(metricCard("今日营收",m_demoMode?"¥ 4,286.50":"加载中…",m_demoMode?"较昨日 +12.6%":"已完成订单实收"));
    cards->addWidget(metricCard("本月营收",m_demoMode?"¥ 128,930.20":"加载中…","已完成订单实收"));
    cards->addWidget(metricCard("累计营收",m_demoMode?"¥ 1,846,720.80":"加载中…","平台历史累计"));
    cards->addWidget(metricCard("今日订单",m_demoMode?"103":"加载中…",m_demoMode?"完成 96 · 待结算 7":"已完成订单口径"));
    cards->addWidget(metricCard("累计充电量",m_demoMode?"1,423,680 kWh":"加载中…","已结束记录口径")); v->addLayout(cards);
    auto *charts = new QHBoxLayout;
    m_trendHost = new QFrame; m_trendHost->setObjectName("panel"); m_statusHost = new QFrame; m_statusHost->setObjectName("panel");
    charts->addWidget(m_trendHost, 2); charts->addWidget(m_statusHost, 1); v->addLayout(charts, 1);
    if(m_demoMode){buildTrendChart(7);buildStatusChart();}else{auto*trendLayout=new QVBoxLayout(m_trendHost);trendLayout->addWidget(new QLabel("正在加载营收趋势……"));auto*statusLayout=new QVBoxLayout(m_statusHost);statusLayout->addWidget(new QLabel("正在加载电桩状态……"));}
    connect(period, &QComboBox::currentIndexChanged, this, [this](int i){ m_trendDays = i == 0 ? 7 : 30; if (m_demoMode) buildTrendChart(m_trendDays); else loadDashboard(m_trendDays); });
    return page;
}

void MainWindow::buildTrendChart(int days)
{
    resetHost(m_trendHost); auto *layout = new QVBoxLayout(m_trendHost); layout->addWidget(new QLabel(QString("营收与订单趋势 · 近 %1 日（自然日缺失补零）").arg(days)));
    auto *revenue = new QLineSeries; revenue->setName("营收（元）"); auto *orders = new QLineSeries; orders->setName("订单数"); const auto trendPoints=MockRepository::instance().trend(days);
    for (const auto &p : trendPoints) { qreal x = QDateTime(p.date, QTime(0,0)).toMSecsSinceEpoch(); revenue->append(x,p.revenue); orders->append(x,p.orders); }
    auto *chart = new QChart; chart->addSeries(revenue); chart->addSeries(orders); chart->legend()->setAlignment(Qt::AlignBottom); chart->setBackgroundVisible(false);
    auto *axisX = new QDateTimeAxis; axisX->setFormat(days == 7 ? "MM-dd" : "MM/dd"); axisX->setTickCount(days == 7 ? 7 : 10); if(!trendPoints.isEmpty())axisX->setRange(QDateTime(trendPoints.first().date,QTime(0,0)),QDateTime(trendPoints.last().date,QTime(23,59,59))); chart->addAxis(axisX, Qt::AlignBottom); revenue->attachAxis(axisX); orders->attachAxis(axisX);
    auto *axisY = new QValueAxis; axisY->setRange(0,6000); axisY->setLabelFormat("%.0f"); axisY->setTitleText("营收（元）"); chart->addAxis(axisY, Qt::AlignLeft); revenue->attachAxis(axisY);
    auto *orderAxis = new QValueAxis; orderAxis->setRange(0,150); orderAxis->setLabelFormat("%d"); orderAxis->setTitleText("订单数"); chart->addAxis(orderAxis,Qt::AlignRight); orders->attachAxis(orderAxis);
    auto *view = new QChartView(chart); view->setRenderHint(QPainter::Antialiasing); layout->addWidget(view);
}

void MainWindow::buildStatusChart()
{
    resetHost(m_statusHost); auto *layout = new QVBoxLayout(m_statusHost); layout->addWidget(new QLabel("电桩状态分布"));
    QMap<QString,int> c{{"IDLE",0},{"RESERVED",0},{"CHARGING",0},{"FAULT",0},{"OFFLINE",0}}; for (const auto &p: MockRepository::instance().piles()) c[p.status]++;
    const int total=std::accumulate(c.cbegin(),c.cend(),0);auto *series = new QPieSeries; for (auto i=c.cbegin();i!=c.cend();++i) series->append(QString("%1  %2 (%3%)").arg(i.key()).arg(i.value()).arg(total?i.value()*100.0/total:0,0,'f',1),i.value()); series->setHoleSize(.48); series->setLabelsVisible();
    auto *chart = new QChart; chart->addSeries(series); chart->setBackgroundVisible(false); chart->legend()->hide();
    auto *view = new QChartView(chart); view->setRenderHint(QPainter::Antialiasing); layout->addWidget(view);
}

void MainWindow::buildTrendChart(const QJsonArray &points)
{
    resetHost(m_trendHost); auto *layout = new QVBoxLayout(m_trendHost);
    layout->addWidget(new QLabel(QString("营收与订单趋势 · 近 %1 日（后端自然日连续补零）").arg(m_trendDays)));
    auto *revenue = new QLineSeries; revenue->setName("营收（元）"); auto *orders = new QLineSeries; orders->setName("订单数"); double maxRevenue=0; int maxOrders=0; QDate firstDate,lastDate;
    for (const auto &value : points) {
        const QJsonObject p = value.toObject(); const QDate date = QDate::fromString(p.value("date").toString(), Qt::ISODate);
        if (!date.isValid()) continue; if(!firstDate.isValid())firstDate=date;lastDate=date;const qreal x = QDateTime(date, QTime(0,0)).toMSecsSinceEpoch();
        const double revenueY=p.value("revenue_cents").toDouble()/100.0;const int orderY=p.value("order_count").toInt();revenue->append(x,revenueY);orders->append(x,orderY);maxRevenue=qMax(maxRevenue,revenueY);maxOrders=qMax(maxOrders,orderY);
    }
    auto *chart = new QChart; chart->addSeries(revenue); chart->addSeries(orders); chart->legend()->setAlignment(Qt::AlignBottom); chart->setBackgroundVisible(false);
    auto *axisX = new QDateTimeAxis; axisX->setFormat(m_trendDays == 7 ? "MM-dd" : "MM/dd"); axisX->setTickCount(m_trendDays == 7 ? 7 : 10);if(firstDate.isValid())axisX->setRange(QDateTime(firstDate,QTime(0,0)),QDateTime(lastDate,QTime(23,59,59))); chart->addAxis(axisX, Qt::AlignBottom); revenue->attachAxis(axisX); orders->attachAxis(axisX);
    auto *axisY = new QValueAxis; axisY->setRange(0,qMax(1.0,maxRevenue*1.15)); axisY->setLabelFormat("%.0f"); axisY->setTitleText("营收（元）"); chart->addAxis(axisY, Qt::AlignLeft); revenue->attachAxis(axisY);
    auto *orderAxis = new QValueAxis; orderAxis->setRange(0,qMax(1.0,maxOrders*1.15)); orderAxis->setLabelFormat("%d"); orderAxis->setTitleText("订单数"); chart->addAxis(orderAxis,Qt::AlignRight); orders->attachAxis(orderAxis);
    auto *view = new QChartView(chart); view->setRenderHint(QPainter::Antialiasing); layout->addWidget(view);
}

void MainWindow::buildStatusChart(const QJsonArray &items)
{
    resetHost(m_statusHost); auto *layout = new QVBoxLayout(m_statusHost); layout->addWidget(new QLabel("电桩状态分布（全量聚合）"));
    QMap<QString,int> counts{{"IDLE",0},{"RESERVED",0},{"CHARGING",0},{"FAULT",0},{"OFFLINE",0}};
    for (const auto &value : items) { const auto item=value.toObject(); counts[item.value("status").toString()] = item.value("count").toInt(); }
    const int total=std::accumulate(counts.cbegin(),counts.cend(),0);auto *series = new QPieSeries; for(auto i=counts.cbegin();i!=counts.cend();++i) series->append(QString("%1  %2 (%3%)").arg(i.key()).arg(i.value()).arg(total?i.value()*100.0/total:0,0,'f',1),i.value()); series->setHoleSize(.48); series->setLabelsVisible();
    auto *chart = new QChart; chart->addSeries(series); chart->setBackgroundVisible(false); chart->legend()->hide(); auto *view=new QChartView(chart); view->setRenderHint(QPainter::Antialiasing); layout->addWidget(view);
}

QWidget *MainWindow::createPilePage()
{
    auto *p=new QWidget; auto *v=new QVBoxLayout(p); v->addWidget(titleLabel("电桩管理")); auto *bar=new QHBoxLayout;
    m_stationFilter=new QComboBox; m_stationFilter->addItem("全部站点"); if(m_demoMode)for(const auto&s:MockRepository::instance().stations())m_stationFilter->addItem(s.name,s.id);
    m_statusFilter=new QComboBox; m_statusFilter->addItems({"全部状态","IDLE","RESERVED","CHARGING","FAULT","OFFLINE"}); m_pileSearch=new QLineEdit; m_pileSearch->setPlaceholderText("输入电桩编号筛选");
    auto *detail=new QPushButton("查看详情"); auto *restart=new QPushButton("模拟远程重启"); restart->setObjectName("dangerButton");
    bar->addWidget(m_stationFilter);bar->addWidget(m_statusFilter);bar->addWidget(m_pileSearch,1);bar->addWidget(detail);bar->addWidget(restart);v->addLayout(bar);
    m_pileTable=new QTableWidget; m_pileTable->setColumnCount(7);m_pileTable->setHorizontalHeaderLabels({"编号","站点","类型","功率(kW)","状态","最近心跳","累计次数"});m_pileTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);m_pileTable->setSelectionBehavior(QAbstractItemView::SelectRows);m_pileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);v->addWidget(m_pileTable);
    auto *pager=new QHBoxLayout;auto *prev=new QPushButton("上一页");auto *next=new QPushButton("下一页");m_pilePageLabel=new QLabel("第 1 / 1 页");pager->addStretch();pager->addWidget(prev);pager->addWidget(m_pilePageLabel);pager->addWidget(next);v->addLayout(pager);
    connect(prev,&QPushButton::clicked,this,[this]{if(m_pilePage>1){--m_pilePage;refreshPiles();}});connect(next,&QPushButton::clicked,this,[this]{if(m_pilePage<m_pilePages){++m_pilePage;refreshPiles();}});
    connect(m_stationFilter,&QComboBox::currentTextChanged,this,[this]{m_pilePage=1;refreshPiles();});connect(m_statusFilter,&QComboBox::currentTextChanged,this,[this]{m_pilePage=1;refreshPiles();});connect(m_pileSearch,&QLineEdit::textChanged,this,[this]{m_pilePage=1;refreshPiles();});connect(detail,&QPushButton::clicked,this,&MainWindow::showPileDetails);connect(restart,&QPushButton::clicked,this,&MainWindow::restartSelectedPile);if(m_demoMode)refreshPiles();return p;
}
void MainWindow::refreshPiles(){if(!m_demoMode){QUrlQuery q;q.addQueryItem("page",QString::number(m_pilePage));q.addQueryItem("page_size","20");if(m_stationFilter->currentIndex()>0)q.addQueryItem("station_id",m_stationFilter->currentData().toString());if(m_statusFilter->currentIndex()>0)q.addQueryItem("status",m_statusFilter->currentText());if(!m_pileSearch->text().trimmed().isEmpty())q.addQueryItem("keyword",m_pileSearch->text().trimmed());m_api->get("/admin/piles?"+q.toString(QUrl::FullyEncoded));return;}QList<Pile> rows;for(const auto&p:MockRepository::instance().piles()){if(m_stationFilter->currentIndex()>0&&p.station!=m_stationFilter->currentText())continue;if(m_statusFilter->currentIndex()>0&&p.status!=m_statusFilter->currentText())continue;if(!p.number.contains(m_pileSearch->text(),Qt::CaseInsensitive))continue;rows.append(p);}const int total=int(rows.size());m_pilePages=qMax(1,(total+4)/5);m_pilePage=qMin(m_pilePage,m_pilePages);m_pilePageLabel->setText(QString("第 %1 / %2 页").arg(m_pilePage).arg(m_pilePages));m_pileTable->setRowCount(0);for(int k=(m_pilePage-1)*5;k<qMin(m_pilePage*5,total);++k){const auto&p=rows[k];int r=m_pileTable->rowCount();m_pileTable->insertRow(r);QStringList x{p.number,p.station,p.type,QString::number(p.power),p.status,p.heartbeat,QString::number(p.sessions)};for(int i=0;i<x.size();++i)m_pileTable->setItem(r,i,new QTableWidgetItem(x[i]));m_pileTable->item(r,0)->setData(Qt::UserRole,p.id);}}
void MainWindow::showPileDetails(){int id=selectedId(m_pileTable);if(id<0){info(this,"提示","请先选择电桩");return;}if(!m_demoMode){m_api->get(QString("/admin/piles/%1").arg(id));return;}for(const auto&p:MockRepository::instance().piles())if(p.id==id){info(this,"电桩详情",QString("编号：%1\n站点：%2\n类型/功率：%3 / %4 kW\n状态：%5\n累计充电：%6 次\n累计时长：%7 小时\n最近心跳：%8\n\n最近状态日志：\n10:20 心跳正常\n09:56 状态同步\n08:31 订单结束").arg(p.number,p.station,p.type).arg(p.power).arg(p.status).arg(p.sessions).arg(p.minutes/60.0,0,'f',1).arg(p.heartbeat));return;}}
void MainWindow::restartSelectedPile(){int id=selectedId(m_pileTable);if(id<0){info(this,"提示","请先选择电桩");return;}const QString status=m_pileTable->item(m_pileTable->currentRow(),4)->text();if(status!="FAULT"&&status!="IDLE"){QMessageBox::warning(this,"状态限制","仅 FAULT 电桩可重启；IDLE 重启为幂等成功");return;}if(QMessageBox::question(this,"危险操作确认","确定向所选电桩发送模拟远程重启指令吗？")!=QMessageBox::Yes)return;if(!m_demoMode){m_api->post(QString("/admin/piles/%1/restart").arg(id));return;}QString msg;bool ok=MockRepository::instance().restartPile(id,msg);if(ok)info(this,"操作成功",msg);else QMessageBox::warning(this,"操作失败",msg);refreshPiles();buildStatusChart();}

QWidget *MainWindow::createStationPage(){auto*p=new QWidget;auto*v=new QVBoxLayout(p);v->addWidget(titleLabel("充电站管理"));auto*b=new QHBoxLayout;auto*detail=new QPushButton("站点及站内详情");auto*add=new QPushButton("新增站点");auto*edit=new QPushButton("修改站点");auto*ap=new QPushButton("新增电桩");b->addStretch();b->addWidget(detail);b->addWidget(add);b->addWidget(edit);b->addWidget(ap);v->addLayout(b);m_stationTable=new QTableWidget;m_stationTable->setColumnCount(8);m_stationTable->setHorizontalHeaderLabels({"名称","地址","坐标","电价(元/kWh)","总桩数","空闲数","在线率","状态"});m_stationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);m_stationTable->setSelectionBehavior(QAbstractItemView::SelectRows);m_stationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);v->addWidget(m_stationTable);auto*pager=new QHBoxLayout;auto*prev=new QPushButton("上一页");auto*next=new QPushButton("下一页");m_stationPageLabel=new QLabel("第 1 / 1 页");pager->addStretch();pager->addWidget(prev);pager->addWidget(m_stationPageLabel);pager->addWidget(next);v->addLayout(pager);connect(prev,&QPushButton::clicked,this,[this]{if(m_stationPage>1){--m_stationPage;refreshStations();}});connect(next,&QPushButton::clicked,this,[this]{if(m_stationPage<m_stationPages){++m_stationPage;refreshStations();}});connect(detail,&QPushButton::clicked,this,&MainWindow::showStationDetails);connect(add,&QPushButton::clicked,this,[this]{editStation(true);});connect(edit,&QPushButton::clicked,this,[this]{editStation(false);});connect(ap,&QPushButton::clicked,this,&MainWindow::addPile);if(m_demoMode)refreshStations();return p;}
void MainWindow::refreshStations(){if(!m_demoMode){m_api->get(QString("/admin/stations?page=%1&page_size=20").arg(m_stationPage));return;}auto&rows=MockRepository::instance().stations();const int total=int(rows.size());m_stationPages=qMax(1,(total+4)/5);m_stationPage=qMin(m_stationPage,m_stationPages);m_stationPageLabel->setText(QString("第 %1 / %2 页").arg(m_stationPage).arg(m_stationPages));m_stationTable->setRowCount(0);for(int k=(m_stationPage-1)*5;k<qMin(m_stationPage*5,total);++k){const auto&s=rows[k];int r=m_stationTable->rowCount();m_stationTable->insertRow(r);QStringList x{s.name,s.address,QString("%1, %2").arg(s.latitude,0,'f',4).arg(s.longitude,0,'f',4),QString::number(s.price,'f',2),QString::number(s.total),QString::number(s.idle),QString::number(s.onlineRate,'f',1)+"%",s.status};for(int i=0;i<x.size();++i)m_stationTable->setItem(r,i,new QTableWidgetItem(x[i]));m_stationTable->item(r,0)->setData(Qt::UserRole,s.id);}}
void MainWindow::showStationDetails(){int id=selectedId(m_stationTable);if(id<0){info(this,"提示","请先选择站点");return;}if(!m_demoMode){int r=m_stationTable->currentRow();QString stationName=m_stationTable->item(r,0)->text();info(this,"站点详情",QString("%1\n地址：%2\n坐标：%3\n电价：¥%4/kWh\n总桩数：%5\n空闲数：%6\n在线率：%7\n状态：%8\n\n关闭此窗口后将自动进入电桩管理并筛选该站点。").arg(stationName,m_stationTable->item(r,1)->text(),m_stationTable->item(r,2)->text(),m_stationTable->item(r,3)->text(),m_stationTable->item(r,4)->text(),m_stationTable->item(r,5)->text(),m_stationTable->item(r,6)->text(),m_stationTable->item(r,7)->text()));int index=m_stationFilter->findData(id);if(index<0){m_stationFilter->addItem(stationName,id);index=m_stationFilter->count()-1;}m_stationFilter->setCurrentIndex(index);ui->navigationList->setCurrentRow(1);return;}for(const auto&s:MockRepository::instance().stations())if(s.id==id){QString piles;for(const auto&p:MockRepository::instance().piles())if(p.station==s.name)piles+=QString("\n• %1  %2  %3 kW").arg(p.number,p.status).arg(p.power);info(this,"站点详情",QString("%1\n地址：%2\n坐标：%3, %4\n电价：¥%5/kWh\n在线率：%6%\n\n站内电桩：%7").arg(s.name,s.address).arg(s.latitude).arg(s.longitude).arg(s.price,0,'f',2).arg(s.onlineRate,0,'f',1).arg(piles.isEmpty()?"\n暂无电桩":piles));return;}}
void MainWindow::editStation(bool create)
{
    auto &list = MockRepository::instance().stations();
    const int id = selectedId(m_stationTable);
    Station *target = nullptr;
    if (m_demoMode) for (auto &s : list) if (s.id == id) target = &s;
    if (!create && id < 0) { info(this, "提示", "请先选择站点"); return; }

    QString oldName, oldAddress;
    QString oldStatus = "ACTIVE";
    double oldLat = 39.960000, oldLng = 116.310000, oldPrice = 1.25;
    if (target) {
        oldName = target->name; oldAddress = target->address;
        oldLat = target->latitude; oldLng = target->longitude; oldPrice = target->price; oldStatus = target->status;
    } else if (!create) {
        const int row = m_stationTable->currentRow();
        oldName = m_stationTable->item(row, 0)->text(); oldAddress = m_stationTable->item(row, 1)->text();
        const QStringList coordinates = m_stationTable->item(row, 2)->text().split(',');
        if (coordinates.size() == 2) { oldLat = coordinates[0].trimmed().toDouble(); oldLng = coordinates[1].trimmed().toDouble(); }
        oldPrice = m_stationTable->item(row, 3)->text().toDouble();
        oldStatus = m_stationTable->item(row, 7)->text();
    }
    QDialog d(this); d.setWindowTitle(create ? "新增站点" : "修改站点"); QFormLayout f(&d);
    QLineEdit name(oldName), addr(oldAddress); QDoubleSpinBox lat, lng, price; QComboBox status; status.addItems({"ACTIVE", "INACTIVE"}); status.setCurrentText(oldStatus);
    lat.setRange(-90, 90); lng.setRange(-180, 180); price.setRange(.01, 20);
    lat.setDecimals(6); lng.setDecimals(6); price.setDecimals(2);
    lat.setValue(oldLat); lng.setValue(oldLng); price.setValue(oldPrice);
    f.addRow("站点名称*", &name); f.addRow("地址*", &addr); f.addRow("纬度*", &lat); f.addRow("经度*", &lng); f.addRow("电价(元/kWh)*", &price); if(!create) f.addRow("状态", &status);
    QDialogButtonBox bb(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); f.addRow(&bb);
    connect(&bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(&bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;
    if (name.text().trimmed().isEmpty() || addr.text().trimmed().isEmpty()) { QMessageBox::warning(this, "校验失败", "站名和地址不能为空"); return; }
    if(!create&&oldStatus=="ACTIVE"&&status.currentText()=="INACTIVE"&&QMessageBox::question(this,"停用站点确认","停用后站点不会出现在用户端，也不能创建新订单。确定继续吗？")!=QMessageBox::Yes)return;
    if (!m_demoMode) {
        QJsonObject body{{"name", name.text().trimmed()}, {"address", addr.text().trimmed()}, {"latitude", lat.value()}, {"longitude", lng.value()}, {"price_cents_per_kwh", qRound64(price.value() * 100)}}; if(!create) body.insert("status",status.currentText());
        if (create) m_api->post("/admin/stations", body); else m_api->put(QString("/admin/stations/%1").arg(id), body);
        return;
    }
    if (create) { int next = list.isEmpty() ? 1 : list.last().id + 1; MockRepository::instance().addStation({next,name.text().trimmed(),addr.text().trimmed(),"ACTIVE",lat.value(),lng.value(),price.value(),0,0,100}); m_stationFilter->addItem(name.text().trimmed(),next); }
    else { const QString previousName=target->name; target->name=name.text().trimmed(); target->address=addr.text().trimmed(); target->latitude=lat.value(); target->longitude=lng.value(); target->price=price.value(); target->status=status.currentText(); for(auto &pile:MockRepository::instance().piles())if(pile.station==previousName)pile.station=target->name;int filterIndex=m_stationFilter->findData(id);if(filterIndex>=0)m_stationFilter->setItemText(filterIndex,target->name); }
    refreshStations(); info(this, "操作成功", create ? "站点已新增" : "站点信息已更新");
}
void MainWindow::addPile(){int sid=selectedId(m_stationTable);if(sid<0){info(this,"提示","请先选择目标站点");return;}const QString stationName=m_stationTable->item(m_stationTable->currentRow(),0)->text();Station*s=nullptr;if(m_demoMode)for(auto&x:MockRepository::instance().stations())if(x.id==sid)s=&x;QDialog d(this);d.setWindowTitle("为“"+stationName+"”新增电桩");QFormLayout f(&d);QLineEdit number;number.setPlaceholderText("例如 BJ-H-0012");QComboBox type;type.addItem("快充","FAST");type.addItem("慢充","SLOW");QDoubleSpinBox power;power.setRange(3,480);power.setValue(120);f.addRow("唯一编号*",&number);f.addRow("类型",&type);f.addRow("额定功率(kW)",&power);QDialogButtonBox bb(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);f.addRow(&bb);connect(&bb,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(&bb,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted)return;QString n=number.text().trimmed();if(n.isEmpty()){QMessageBox::warning(this,"校验失败","电桩编号不能为空");return;}if(!m_demoMode){m_api->post(QString("/admin/stations/%1/piles").arg(sid),{{"pile_no",n},{"pile_type",type.currentData().toString()},{"rated_power_w",qRound64(power.value()*1000)}});return;}for(const auto&p:MockRepository::instance().piles())if(p.number==n){QMessageBox::warning(this,"校验失败","电桩编号必须唯一");return;}auto&ps=MockRepository::instance().piles();int next=ps.isEmpty()?1:ps.last().id+1;MockRepository::instance().addPile({next,n,s->name,type.currentText(),"IDLE","刚刚",power.value(),0,0});s->total++;s->idle++;refreshStations();refreshPiles();buildStatusChart();info(this,"操作成功","新电桩已加入站点");}

QWidget *MainWindow::createUserPage(){auto*p=new QWidget;auto*v=new QVBoxLayout(p);v->addWidget(titleLabel("用户管理"));auto*b=new QHBoxLayout;m_userSearch=new QLineEdit;m_userSearch->setPlaceholderText("手机号模糊查询（支持输入中间四位）");auto*detail=new QPushButton("用户详情");auto*toggle=new QPushButton("冻结 / 解冻");toggle->setObjectName("dangerButton");b->addWidget(m_userSearch,1);b->addWidget(detail);b->addWidget(toggle);v->addLayout(b);m_userTable=new QTableWidget;m_userTable->setColumnCount(7);m_userTable->setHorizontalHeaderLabels({"用户ID","手机号（脱敏）","昵称","余额","注册时间","状态","订单数"});m_userTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);v->addWidget(m_userTable);auto*pager=new QHBoxLayout;auto*prev=new QPushButton("上一页");auto*next=new QPushButton("下一页");m_userPageLabel=new QLabel("第 1 / 1 页");pager->addStretch();pager->addWidget(prev);pager->addWidget(m_userPageLabel);pager->addWidget(next);v->addLayout(pager);connect(prev,&QPushButton::clicked,this,[this]{if(m_userPage>1){--m_userPage;refreshUsers();}});connect(next,&QPushButton::clicked,this,[this]{if(m_userPage<m_userPages){++m_userPage;refreshUsers();}});connect(m_userSearch,&QLineEdit::textChanged,this,[this]{m_userPage=1;refreshUsers();});connect(detail,&QPushButton::clicked,this,&MainWindow::showUserDetails);connect(toggle,&QPushButton::clicked,this,&MainWindow::toggleUserStatus);if(m_demoMode)refreshUsers();return p;}
void MainWindow::refreshUsers(){if(!m_demoMode){QUrlQuery q;q.addQueryItem("page",QString::number(m_userPage));q.addQueryItem("page_size","20");if(!m_userSearch->text().trimmed().isEmpty())q.addQueryItem("phone_keyword",m_userSearch->text().trimmed());m_api->get("/admin/users?"+q.toString(QUrl::FullyEncoded));return;}QList<User> rows;for(const auto&u:MockRepository::instance().users())if(u.phone.contains(m_userSearch->text(),Qt::CaseInsensitive))rows.append(u);const int total=int(rows.size());m_userPages=qMax(1,(total+4)/5);m_userPage=qMin(m_userPage,m_userPages);m_userPageLabel->setText(QString("第 %1 / %2 页").arg(m_userPage).arg(m_userPages));m_userTable->setRowCount(0);for(int k=(m_userPage-1)*5;k<qMin(m_userPage*5,total);++k){const auto&u=rows[k];int r=m_userTable->rowCount();m_userTable->insertRow(r);QStringList x{QString::number(u.id),u.phone,u.nickname,"¥"+QString::number(u.balance,'f',2),u.registered,u.status,QString::number(u.orders)};for(int i=0;i<x.size();++i)m_userTable->setItem(r,i,new QTableWidgetItem(x[i]));m_userTable->item(r,0)->setData(Qt::UserRole,u.id);}}
void MainWindow::showUserDetails(){int id=selectedId(m_userTable);if(id<0){info(this,"提示","请先选择用户");return;}if(!m_demoMode){m_api->get(QString("/admin/users/%1").arg(id));return;}for(const auto&u:MockRepository::instance().users())if(u.id==id){info(this,"用户详情",QString("用户ID：%1\n手机号：%2\n昵称：%3\n余额：¥%4\n状态：%5\n\n订单数：%6\n累计消费：¥%7\n最近订单：#20260903086 已完成 ¥42.60\n充值摘要：2026-08-30 +¥200.00").arg(u.id).arg(u.phone,u.nickname).arg(u.balance,0,'f',2).arg(u.status).arg(u.orders).arg(u.spent,0,'f',2));return;}}
void MainWindow::toggleUserStatus(){int id=selectedId(m_userTable);if(id<0){info(this,"提示","请先选择用户");return;}QString current=m_userTable->item(m_userTable->currentRow(),5)->text();bool freeze=current=="NORMAL";QString verb=freeze?"冻结":"解冻";QString phone=m_userTable->item(m_userTable->currentRow(),1)->text();if(QMessageBox::question(this,verb+"确认",QString("确定%1用户 %2 吗？\n\n冻结操作会由后端检查该用户是否存在未完成订单；重复操作按幂等成功处理。").arg(verb,phone))!=QMessageBox::Yes)return;if(!m_demoMode){m_api->post(QString("/admin/users/%1/%2").arg(id).arg(freeze?"freeze":"unfreeze"));return;}for(auto&u:MockRepository::instance().users())if(u.id==id){if(freeze&&u.hasActiveOrder){QMessageBox::warning(this,"操作冲突","用户存在未完成订单，暂时不能冻结");return;}u.status=freeze?"FROZEN":"NORMAL";refreshUsers();info(this,"操作成功","用户已"+verb);return;}}

void MainWindow::loadDashboard(int days)
{
    if (m_demoMode) return;
    m_api->get(QString("/admin/revenue?days=%1").arg(days));
    m_api->get("/dashboard/pile-status");
}

static QString maskedPhone(QString phone)
{
    if (phone.size() == 11) phone.replace(3, 4, "****");
    return phone;
}

static QString jsonSummary(const QJsonValue &data)
{
    if (data.isObject()) return QString::fromUtf8(QJsonDocument(data.toObject()).toJson(QJsonDocument::Indented));
    if (data.isArray()) return QString::fromUtf8(QJsonDocument(data.toArray()).toJson(QJsonDocument::Indented));
    return data.toVariant().toString();
}

static QString pileDetailText(const QJsonObject &data)
{
    QJsonObject pile = data.value("pile").toObject(); if (pile.isEmpty()) pile = data;
    const int count = data.value("total_charge_count").toInt(pile.value("total_charge_count").toInt());
    const double hours = data.value("total_charge_duration_seconds").toDouble(pile.value("total_charge_duration_seconds").toDouble()) / 3600.0;
    QString text = QString("编号：%1\n站点：%2\n类型：%3\n额定功率：%4 kW\n状态：%5\n最近心跳：%6\n累计充电：%7 次\n累计时长：%8 小时")
        .arg(pile.value("pile_no").toString(), pile.value("station_name").toString(), pile.value("pile_type").toString())
        .arg(pile.value("rated_power_w").toDouble()/1000.0,0,'f',1).arg(pile.value("status").toString(),pile.value("last_heartbeat_at").toString()).arg(count).arg(hours,0,'f',1);
    const QJsonArray logs=data.value("status_logs").toArray(); if(!logs.isEmpty()){text+="\n\n最近状态日志：";for(const auto&v:logs){const auto log=v.toObject();text+=QString("\n%1  %2 → %3  %4").arg(log.value("created_at").toString(),log.value("old_status").toString(),log.value("new_status").toString(),log.value("reason").toString());}}
    return text;
}

static QString userDetailText(const QJsonObject &data)
{
    QJsonObject user=data.value("user").toObject();if(user.isEmpty())user=data;
    int orderCount=data.value("order_count").toInt(data.value("total_order_count").toInt());
    double spent=data.value("total_spent_cents").toDouble()/100.0;
    QString text=QString("用户ID：%1\n手机号：%2\n昵称：%3\n余额：¥%4\n注册时间：%5\n状态：%6\n\n订单数：%7\n累计消费：¥%8")
        .arg(user.value("id").toInt()).arg(user.value("phone").toString(),user.value("nickname").toString()).arg(user.value("balance_cents").toDouble()/100.0,0,'f',2).arg(user.value("created_at").toString(),user.value("status").toString()).arg(orderCount).arg(spent,0,'f',2);
    QJsonArray orders=data.value("recent_orders").toArray();if(!orders.isEmpty()){text+="\n\n最近订单：";for(const auto&v:orders){auto o=v.toObject();text+=QString("\n%1  %2  ¥%3").arg(o.value("order_no").toString(),o.value("status").toString()).arg(o.value("amount_cents").toDouble()/100.0,0,'f',2);}}
    QJsonArray records=data.value("recharge_records").toArray();if(records.isEmpty())records=data.value("recent_recharge_records").toArray();if(!records.isEmpty()){text+="\n\n充值摘要：";for(const auto&v:records){auto rr=v.toObject();text+=QString("\n%1  +¥%2").arg(rr.value("created_at").toString()).arg(rr.value("amount_cents").toDouble()/100.0,0,'f',2);}}
    return text;
}

void MainWindow::handleApiSuccess(const QString &path, const QJsonValue &data, const QJsonObject &)
{
    ui->statusbar->showMessage("数据已更新 · " + path, 4000);
    if (path.startsWith("/admin/revenue?")) {
        if (!path.contains(QString("days=%1").arg(m_trendDays))) return;
        const QJsonObject d = data.toObject();
        if (m_metricValues.size() >= 5) {
            m_metricValues[0]->setText(QString("¥ %1").arg(d.value("today_revenue_cents").toDouble()/100.0,0,'f',2));
            m_metricValues[1]->setText(QString("¥ %1").arg(d.value("month_revenue_cents").toDouble()/100.0,0,'f',2));
            m_metricValues[2]->setText(QString("¥ %1").arg(d.value("total_revenue_cents").toDouble()/100.0,0,'f',2));
            m_metricValues[3]->setText(QString::number(d.value("today_order_count").toInt()));
            m_metricValues[4]->setText(QString("%1 kWh").arg(d.value("total_energy_wh").toDouble()/1000.0,0,'f',1));
        }
        buildTrendChart(d.value("trend").toArray()); return;
    }
    if (path == "/dashboard/pile-status") {
        const QJsonObject d=data.toObject(); buildStatusChart(d.value("items").toArray()); return;
    }
    if (path.startsWith("/admin/piles?")) {
        const QJsonObject d=data.toObject(); const QJsonArray items=d.value("items").toArray(); const QJsonObject pg=d.value("pagination").toObject();
        m_pilePages=qMax(1,pg.value("total_pages").toInt(1)); m_pilePage=pg.value("page").toInt(m_pilePage); m_pilePageLabel->setText(QString("第 %1 / %2 页 · 共 %3 条").arg(m_pilePage).arg(m_pilePages).arg(pg.value("total").toInt()));
        m_pileTable->setRowCount(0); for(const auto&v:items){const auto p=v.toObject();int r=m_pileTable->rowCount();m_pileTable->insertRow(r);QStringList x{p.value("pile_no").toString(),p.value("station_name").toString(),p.value("pile_type").toString(),QString::number(p.value("rated_power_w").toDouble()/1000.0,'f',1),p.value("status").toString(),p.value("last_heartbeat_at").toString(),QString::number(p.value("total_charge_count").toInt())};for(int i=0;i<x.size();++i)m_pileTable->setItem(r,i,new QTableWidgetItem(x[i]));m_pileTable->item(r,0)->setData(Qt::UserRole,p.value("id").toInt());} return;
    }
    if (path.startsWith("/admin/stations?")) {
        const QJsonObject d=data.toObject();const QJsonArray items=d.value("items").toArray();const QJsonObject pg=d.value("pagination").toObject();m_stationPages=qMax(1,pg.value("total_pages").toInt(1));m_stationPage=pg.value("page").toInt(m_stationPage);m_stationPageLabel->setText(QString("第 %1 / %2 页 · 共 %3 条").arg(m_stationPage).arg(m_stationPages).arg(pg.value("total").toInt()));m_stationTable->setRowCount(0);
        bool rebuildFilter=m_stationFilter->count()==1;for(const auto&v:items){const auto s=v.toObject();int r=m_stationTable->rowCount();m_stationTable->insertRow(r);QString coords=(s.value("latitude").isNull()||s.value("longitude").isNull())?"未配置":QString("%1, %2").arg(s.value("latitude").toDouble(),0,'f',6).arg(s.value("longitude").toDouble(),0,'f',6);QString price=s.value("price_cents_per_kwh").isNull()?"未配置":QString::number(s.value("price_cents_per_kwh").toDouble()/100.0,'f',2);QStringList x{s.value("name").toString(),s.value("address").toString(),coords,price,QString::number(s.value("total_piles").toInt()),QString::number(s.value("available_piles").toInt()),QString::number(s.value("online_rate").toDouble(),'f',1)+"%",s.value("status").toString()};for(int i=0;i<x.size();++i)m_stationTable->setItem(r,i,new QTableWidgetItem(x[i]));int id=s.value("id").toInt();m_stationTable->item(r,0)->setData(Qt::UserRole,id);if(rebuildFilter)m_stationFilter->addItem(s.value("name").toString(),id);}return;
    }
    if (path.startsWith("/admin/users?")) {
        const QJsonObject d=data.toObject();const QJsonArray items=d.value("items").toArray();const QJsonObject pg=d.value("pagination").toObject();m_userPages=qMax(1,pg.value("total_pages").toInt(1));m_userPage=pg.value("page").toInt(m_userPage);m_userPageLabel->setText(QString("第 %1 / %2 页 · 共 %3 条").arg(m_userPage).arg(m_userPages).arg(pg.value("total").toInt()));m_userTable->setRowCount(0);for(const auto&v:items){const auto u=v.toObject();int r=m_userTable->rowCount();m_userTable->insertRow(r);QStringList x{QString::number(u.value("id").toInt()),maskedPhone(u.value("phone").toString()),u.value("nickname").toString(),QString("¥%1").arg(u.value("balance_cents").toDouble()/100.0,0,'f',2),u.value("created_at").toString(),u.value("status").toString(),QString::number(u.value("order_count").toInt())};for(int i=0;i<x.size();++i)m_userTable->setItem(r,i,new QTableWidgetItem(x[i]));m_userTable->item(r,0)->setData(Qt::UserRole,u.value("id").toInt());}return;
    }
    if (path.contains("/restart")) { info(this,"操作成功","远程重启指令执行成功"); refreshPiles(); loadDashboard(m_trendDays); return; }
    if (path.contains("/freeze") || path.contains("/unfreeze")) { info(this,"操作成功","用户状态已更新"); refreshUsers(); return; }
    if (path.startsWith("/admin/piles/")) { info(this,"电桩详情",data.isObject()?pileDetailText(data.toObject()):jsonSummary(data)); return; }
    if (path.startsWith("/admin/users/")) { info(this,"用户详情",data.isObject()?userDetailText(data.toObject()):jsonSummary(data)); return; }
    if (path == "/admin/stations" || path.startsWith("/admin/stations/")) { info(this,"操作成功","站点或电桩数据已保存"); refreshStations(); refreshPiles(); return; }
}

void MainWindow::handleApiFailure(const QString &path, int httpStatus, int code, const QString &message)
{
    ui->statusbar->showMessage(QString("请求失败 · HTTP %1 · code %2").arg(httpStatus).arg(code), 8000);
    QMessageBox::warning(this, "操作失败", message + QString("\n\n接口：%1\nHTTP：%2　业务码：%3").arg(path).arg(httpStatus).arg(code));
}
