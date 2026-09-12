#include "dashboardserver.h"

#include "business.h"
#include "forecastworker.h"
#include "analyticsresult.h"

#include <QFile>
#include <QJsonDocument>
#include <QTcpSocket>

DashboardServer::DashboardServer(Business &business, QObject *parent)
    : QTcpServer(parent), m_business(business), m_worker(new ForecastWorker)
{
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread,&QThread::finished,m_worker,&QObject::deleteLater);
    connect(m_worker,&ForecastWorker::ready,this,[this](const QJsonObject &result){m_data=result;});
    m_workerThread.start();
    m_refreshTimer.setInterval(5000);
    connect(&m_refreshTimer,&QTimer::timeout,this,&DashboardServer::refresh);
    connect(this,&QTcpServer::newConnection,this,[this]{
        while(hasPendingConnections()) {
            auto *socket=nextPendingConnection();socket->setParent(this);
            connect(socket,&QTcpSocket::readyRead,this,[this,socket]{
                QByteArray request=socket->property("request").toByteArray()+socket->readAll();
                if(request.size()>8192){socket->abort();return;}
                if(!request.contains("\r\n\r\n")){socket->setProperty("request",request);return;}
                if(!request.startsWith("GET ")){socket->disconnectFromHost();return;}
                const QByteArray path=request.split(' ').value(1).split('?').value(0);
                QByteArray body,type,status="200 OK";
                if(path=="/api/dashboard") {body=QJsonDocument(m_data).toJson(QJsonDocument::Compact);type="application/json; charset=utf-8";}
                else if(path=="/api/analytics") {
                    bool valid=false;
                    int age=qEnvironmentVariable("ANALYTICS_MAX_AGE_SECONDS","900").toInt(&valid);
                    if(!valid || age<1) age=900;
                    const auto result=readAnalyticsResult(qEnvironmentVariable("ANALYTICS_RESULT_PATH"),age);
                    if(!result["available"].toBool()) status="503 Service Unavailable";
                    body=QJsonDocument(result).toJson(QJsonDocument::Compact);
                    type="application/json; charset=utf-8";
                }
                else if(path=="/"||path=="/dashboard.html") {QFile file(":/web/dashboard.html");if(file.open(QIODevice::ReadOnly))body=file.readAll();type="text/html; charset=utf-8";}
                else {status="404 Not Found";body="Not found";type="text/plain; charset=utf-8";}
                const QByteArray header="HTTP/1.1 "+status+"\r\nContent-Type: "+type+"\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
                socket->write(header+body);socket->disconnectFromHost();
            });
        }
    });
    refresh();m_refreshTimer.start();
}

DashboardServer::~DashboardServer()
{
    m_workerThread.quit();m_workerThread.wait();
}

void DashboardServer::refresh()
{
    const QJsonObject input=m_business.analyticsInput();
    QMetaObject::invokeMethod(m_worker,"calculate",Qt::QueuedConnection,Q_ARG(QJsonObject,input));
}
