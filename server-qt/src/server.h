#pragma once
#include "business.h"
#include "maps.h"

class Server:public QTcpServer {
public:
    Server(Database &db,QString mapKey,QObject *parent=nullptr);
    Business business;
    Maps maps;
private:QTimer sweep;
};
