// 声明 TCP Socket 服务端及其业务、地图和定期清理组件。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#pragma once
#include "business.h"
#include "maps.h"

// 创建服务端并启动过期清理定时器及新连接处理。
class Server:public QTcpServer {
public:
// 创建服务端并启动过期清理定时器及新连接处理。
    Server(Database &db,QString mapKey,QObject *parent=nullptr);
    Business business;
    Maps maps;
private:QTimer sweep;
};
