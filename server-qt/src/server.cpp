// 实现客户端连接生命周期、带长度前缀的帧协议、请求路由和响应校验。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "server.h"
#include <QtEndian>
#include <QStringDecoder>

class Connection:public QObject {
public:
    Connection(QTcpSocket *socket,Server &server):QObject(&server),socket(socket),server(server){
        socket->setParent(this);socket->setReadBufferSize(2*1048576);
        connect(socket,&QTcpSocket::readyRead,this,[this]{read();});
        connect(socket,&QTcpSocket::disconnected,this,&QObject::deleteLater);
        connect(socket,&QTcpSocket::bytesWritten,this,[this]{flush();});
        deadline.setSingleShot(true);connect(&deadline,&QTimer::timeout,this,[this]{this->socket->abort();});
    }
private:
    QTcpSocket *socket;Server &server;QByteArray input,output;QSet<QString> pending;QTimer deadline;bool flushing=false;
/// 实现 flush 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void flush(){if(flushing)return;flushing=true;while(!output.isEmpty()&&socket->state()==QAbstractSocket::ConnectedState){auto n=socket->write(output);if(n<0){socket->abort();break;}if(!n)break;output.remove(0,n);}flushing=false;}
/// 实现 respond 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void respond(QJsonObject request,QJsonValue data,int code){
        if(socket->state()!=QAbstractSocket::ConnectedState)return;
        if(code==0) {
            const auto spec=contract()["x-actions"].toObject()[request["action"].toString()].toObject();
            if(spec.isEmpty() || !validate(data,spec["response"].toObject())) code=50000;
        }
        QString message="success";if(code){try{fail(code);}catch(const Failure&e){message=e.message;}data=QJsonValue::Null;}
        QJsonObject response{{"version",1},{"request_id",request["request_id"]},{"action",request["action"].toString()},{"code",code},{"message",message},{"data",data}};
        auto bytes=frame(response);
        if(bytes.size()>1048580){response["data"]=QJsonValue::Null;response["code"]=50000;response["message"]=QStringLiteral("响应过大，请减少分页数量");bytes=frame(response);}
        pending.remove(request["request_id"].toString());
        if(output.size()+socket->bytesToWrite()+bytes.size()>2*1048576){socket->abort();return;}
        output+=bytes;flush();
    }
/// 实现 handle 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void handle(QJsonObject request){
        QString id=request["request_id"].toString();static const QRegularExpression validId("^[A-Za-z0-9_-]{1,64}$");
        if(!validId.match(id).hasMatch()||pending.contains(id)||pending.size()>=64){socket->abort();return;}
        pending.insert(id);
        try{
            if(!request["version"].isDouble()) fail(40001);
            if(integer(request["version"])!=1) fail(40010);
            if(!validate(request,contract()["$defs"].toObject()["request"].toObject()))fail(40001);
            auto a=request["action"].toString();auto spec=contract()["x-actions"].toObject()[a].toObject();if(spec.isEmpty())fail(40009);
            auto data=request["data"].toObject();if(!validate(data,spec["request"].toObject()))fail(40001);
            auto identity=server.business.authorize(a,request["token"].toString());
            if(a.startsWith("map.")||a=="stations.nearby") {
                QPointer<Connection> self(this);auto candidates=a=="stations.nearby"?server.business.nearbyCandidates():QJsonArray();
                server.maps.run(a,data,candidates,[self,request](auto result,int code){if(self)self->respond(request,result,code);});
            }else respond(request,server.business.dispatch(a,data,identity),0);
        }catch(const Failure &e){respond(request,{},e.code);}catch(...){respond(request,{},50000);}
    }
/// 实现 read 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void read(){
        while(socket->bytesAvailable()>0){
            if(input.isEmpty())deadline.start(15000);
            input+=socket->read(qMin<qint64>(65536,socket->bytesAvailable()));
            while(input.size()>=4){
                quint32 length=qFromBigEndian<quint32>(input.constData());if(length==0||length>1048576){socket->abort();return;}
                if(input.size()<qint64(length)+4)break;
                auto bytes=input.mid(4,length);input.remove(0,length+4);deadline.stop();if(!input.isEmpty())deadline.start(15000);
                QStringDecoder decoder(QStringDecoder::Utf8);decoder.decode(bytes);auto document=QJsonDocument::fromJson(bytes);
                if(decoder.hasError()||!document.isObject()){socket->abort();return;}
                handle(document.object());if(socket->state()!=QAbstractSocket::ConnectedState)return;
            }
        }
    }
};
/// 创建服务端并启动过期清理定时器及新连接处理。
Server::Server(Database &db,QString key,QObject *parent):QTcpServer(parent),business(db),maps(key,this){
    business.expire();sweep.setInterval(30000);
    connect(&sweep,&QTimer::timeout,this,[this]{try{business.expire();}catch(const Failure&){qWarning("Reservation cleanup failed");}});sweep.start();
    connect(this,&QTcpServer::newConnection,this,[this]{while(hasPendingConnections())new Connection(nextPendingConnection(),*this);});
}
