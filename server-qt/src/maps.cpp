// 实现地图 HTTP 请求、超时校验、距离计算、批量附近站点查询和结果缓存。
// 本文件中的注释仅用于说明逻辑，不改变可执行代码。

#include "maps.h"
#include <cmath>

namespace {
/// 实现 number 的本地处理逻辑，保持与项目其他模块的接口约定一致。
double number(QJsonValue v,double low=0,double high=9007199254740991.0){if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<low||v.toDouble()>high)fail(50301);return v.toDouble();}
/// 实现 whole 的本地处理逻辑，保持与项目其他模块的接口约定一致。
qint64 whole(QJsonValue value) { const double n=number(value); if(std::floor(n)!=n) fail(50301); return qint64(n); }
class MapJob:public QObject {
public:
    MapJob(QNetworkAccessManager &manager,QString key,QUrl base,int timeout,int total,Result done,QObject *parent):QObject(parent),manager(manager),key(key),base(base),timeout(timeout),done(done){deadline.setSingleShot(true);connect(&deadline,&QTimer::timeout,this,[this]{finish({},50301);});deadline.start(total);}
    ~MapJob(){if(reply&&!reply->isFinished())reply->abort();}
/// 实现 finish 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void finish(QJsonValue data,int code=0){if(finished)return;finished=true;deadline.stop();if(reply&&!reply->isFinished())reply->abort();done(data,code);deleteLater();}
/// 发起 GET 风格请求。
    void get(const QString &path,QUrlQuery params,std::function<void(QJsonObject)> callback){
        if(finished) return;
        params.addQueryItem("key",key);auto url=base;url.setPath(path);url.setQuery(params);
        QNetworkRequest request(url);request.setTransferTimeout(timeout);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        reply=manager.get(request);auto r=reply;
        auto timer=new QTimer(r);timer->setSingleShot(true);connect(timer,&QTimer::timeout,r,[r]{r->abort();});timer->start(timeout);
        connect(r,&QNetworkReply::readyRead,this,[this,r]{if(r->bytesAvailable()>1048576)finish({},50301);});
        connect(r,&QNetworkReply::finished,this,[this,r,callback,timer]{
            timer->stop();r->deleteLater();if(finished)return;
            auto doc=QJsonDocument::fromJson(r->readAll());
            const int httpStatus=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const int upstreamStatus=doc.isObject()?doc.object()["status"].toInt(-1):-1;
            if(r->error()!=QNetworkReply::NoError||httpStatus!=200||!doc.isObject()||upstreamStatus!=0){
                qWarning().noquote()<<"Tencent map request failed:"<<"network="<<int(r->error())<<r->errorString()<<"http="<<httpStatus<<"status="<<upstreamStatus<<"message="<<(doc.isObject()?doc.object()["message"].toString():QStringLiteral("invalid JSON"));
                finish({},50301);return;
            }
            try{callback(doc.object()["result"].toObject());}catch(const Failure&){finish({},50301);}
        });
    }
/// 实现 getImage 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void getImage(const QString &path,QUrlQuery params){
        if(finished)return;
        params.addQueryItem("key",key);auto url=base;url.setPath(path);url.setQuery(params);
        QNetworkRequest request(url);request.setTransferTimeout(timeout);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        reply=manager.get(request);auto r=reply;
        auto timer=new QTimer(r);timer->setSingleShot(true);connect(timer,&QTimer::timeout,r,[r]{r->abort();});timer->start(timeout);
        connect(r,&QNetworkReply::finished,this,[this,r,timer]{
            timer->stop();r->deleteLater();if(finished)return;const auto bytes=r->readAll();const int httpStatus=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if(r->error()!=QNetworkReply::NoError||httpStatus!=200||bytes.size()>700000||!bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))){qWarning().noquote()<<"Tencent static map request failed:"<<"network="<<int(r->error())<<r->errorString()<<"http="<<httpStatus<<"bytes="<<bytes.size();finish({},50301);return;}
            finish(QJsonObject{{"content_type","image/png"},{"content_base64",QString::fromLatin1(bytes.toBase64())},{"width",600},{"height",300}});
        });
    }
/// 实现 batch 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    void batch(){
        if(offset>=stations.size()){
            if(items.isEmpty()&&!stations.isEmpty()){finish({},50301);return;}
/// 实现 sort 的本地处理逻辑，保持与项目其他模块的接口约定一致。
            std::sort(items.begin(),items.end(),[](const auto &a,const auto &b){return a["route_distance_meters"].toDouble()<b["route_distance_meters"].toDouble();});
            QJsonArray selected;auto start=(data["page"].toInteger(1)-1)*data["page_size"].toInteger(20);auto end=qMin<qint64>(items.size(),start+data["page_size"].toInteger(20));
            for(auto i=start;i<end;++i)selected.append(items[i]);
            finish(QJsonObject{{"items",selected},{"page",data["page"].toInteger(1)},{"page_size",data["page_size"].toInteger(20)},{"total",items.size()}});return;
        }
        int count=qMin(200,int(stations.size())-offset);QStringList destinations;
        for(int i=0;i<count;++i){auto s=stations[offset+i].toObject();destinations<<QString::number(s["latitude"].toDouble(),'f',7)+","+QString::number(s["longitude"].toDouble(),'f',7);}
        get("/ws/distance/v1/matrix",QUrlQuery{{"mode","driving"},{"from",point(data,"latitude","longitude")},{"to",destinations.join(';')}},[this,count](auto result){
            auto rows=result["rows"].toArray();if(rows.size()!=1)fail(50301);auto elements=rows[0].toObject()["elements"].toArray();if(elements.size()!=count)fail(50301);
            for(int i=0;i<count;++i){if(!elements[i].isObject())fail(50301);auto e=elements[i].toObject();if((e.contains("status")&&e["status"]!=0)||!e.contains("duration"))continue;
                auto s=stations[offset+i].toObject();const auto distance=whole(e["distance"]);if(distance>data["radius_km"].toDouble(100.0)*1000.0)continue;s["route_distance_meters"]=distance;s["route_duration_seconds"]=whole(e["duration"]);items<<s;
            }offset+=count;batch();
        });
    }
/// 实现 point 的本地处理逻辑，保持与项目其他模块的接口约定一致。
    static QString point(QJsonObject d,QString lat,QString lng){return QString::number(d[lat].toDouble(),'f',7)+","+QString::number(d[lng].toDouble(),'f',7);}
    QJsonObject data;QJsonArray stations;QList<QJsonObject> items;int offset=0;
private:
    QNetworkAccessManager &manager;QString key;QUrl base;int timeout;Result done;QTimer deadline;QPointer<QNetworkReply> reply;bool finished=false;
};
}
/// 持有地图 API 配置、网络管理器和短期响应缓存。
Maps::Maps(QString key,QObject *parent,QUrl base,int requestTimeout,int totalTimeout):QObject(parent),key(key),base(base),requestTimeout(requestTimeout),totalTimeout(totalTimeout){}
/// 根据 action 调用地图服务，完成参数处理、结果转换和缓存命中/写入。
void Maps::run(const QString &a,const QJsonObject &d,const QJsonArray &stations,Result done) {
    if(a=="stations.nearby"&&stations.isEmpty()){done(QJsonObject{{"items",QJsonArray{}},{"page",d["page"].toInteger(1)},{"page_size",d["page_size"].toInteger(20)},{"total",0}},0);return;}
    if(key.trimmed().isEmpty()){done({},50301);return;}
    QJsonArray selectedStations=stations;
    if(a=="stations.nearby"){
        QList<QPair<double,QJsonObject>> candidates;const double lat=d["latitude"].toDouble(),lng=d["longitude"].toDouble(),lngScale=std::cos(lat*0.017453292519943295);
        for(const auto &value:stations){const auto station=value.toObject();const double dy=station["latitude"].toDouble()-lat,dx=(station["longitude"].toDouble()-lng)*lngScale;candidates.append({dx*dx+dy*dy,station});}
/// 实现 sort 的本地处理逻辑，保持与项目其他模块的接口约定一致。
        std::sort(candidates.begin(),candidates.end(),[](const auto &left,const auto &right){return left.first<right.first;});selectedStations={};
        const qint64 candidateLimit=d["page"].toInteger(1)*d["page_size"].toInteger(20);
        for(qint64 i=0;i<qMin<qint64>(candidateLimit,candidates.size());++i)selectedStations.append(candidates[i].second);
    }
    const QByteArray cacheBytes=QJsonDocument(QJsonObject{{"action",a},{"data",d},{"stations",selectedStations}}).toJson(QJsonDocument::Compact);
    const QString cacheKey=QString::fromLatin1(QCryptographicHash::hash(cacheBytes,QCryptographicHash::Sha256).toHex());
    const qint64 now=QDateTime::currentMSecsSinceEpoch();
    if(const auto it=cache.constFind(cacheKey);it!=cache.cend()&&it->expiresAtMs>now){done(it->data,0);return;}
    cache.remove(cacheKey);
    Result cachedDone=[this,cacheKey,done](QJsonValue data,int code){
        if(code==0){if(cache.size()>=128)cache.clear();cache.insert(cacheKey,{QDateTime::currentMSecsSinceEpoch()+600000,data});}
        done(data,code);
    };
    auto job=new MapJob(network,key,base,requestTimeout,totalTimeout,cachedDone,this);job->data=d;job->stations=selectedStations;
    if(a=="stations.nearby"){
        job->batch();return;
    }
    if(a=="map.geocode") {
        auto address=d["address"].toString().trimmed();if(address.isEmpty()){job->finish({},40001);return;}
        job->get("/ws/geocoder/v1/",QUrlQuery{{"address",address}},[job,address](auto result){auto loc=result["location"].toObject();job->finish(QJsonObject{{"latitude",number(loc["lat"],-90,90)},{"longitude",number(loc["lng"],-180,180)},{"formatted_address",result["title"].toString(address)}});});return;
    }
    if(a=="map.snapshot") {job->getImage("/ws/staticmap/v2/",QUrlQuery{{"center",MapJob::point(d,"latitude","longitude")},{"zoom",QString::number(d["zoom"].toInt(12))},{"size","600*300"},{"maptype","roadmap"}});return;}
    const auto mode=d["mode"].toString("driving");
    job->get("/ws/direction/v1/"+mode,QUrlQuery{{"from",MapJob::point(d,"from_latitude","from_longitude")},{"to",MapJob::point(d,"to_latitude","to_longitude")}},[job,mode](auto result){
        auto routes=result["routes"].toArray();if(routes.isEmpty())fail(50301);auto r=routes[0].toObject();auto poly=r["polyline"].toArray();if(poly.size()<2||poly.size()%2)fail(50301);
        QList<double> coordinates;for(int i=0;i<poly.size();++i){double value=number(poly[i],-9007199254740991.0);if(i>=2)value=coordinates[i-2]+value/1000000.0;if(std::abs(value)>(i%2?180:90))fail(50301);coordinates<<value;}
        QJsonArray points;for(int i=0;i<coordinates.size();i+=2)points.append(QJsonObject{{"latitude",coordinates[i]},{"longitude",coordinates[i+1]}});
        job->finish(QJsonObject{{"mode",mode},{"distance_meters",whole(r["distance"])},{"duration_seconds",qint64(std::round(number(r["duration"],0,10000000)*60))},{"route_points",points}});
    });
}
