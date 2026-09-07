#pragma once

#include <QJsonObject>
#include <QTcpServer>
#include <QThread>
#include <QTimer>

class Business;
class ForecastWorker;

class DashboardServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit DashboardServer(Business &business, QObject *parent = nullptr);
    ~DashboardServer() override;
private:
    void refresh();
    Business &m_business;
    ForecastWorker *m_worker = nullptr;
    QThread m_workerThread;
    QTimer m_refreshTimer;
    QJsonObject m_data;
};
