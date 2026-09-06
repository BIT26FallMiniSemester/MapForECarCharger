#include "maps.h"
#include <cmath>

namespace {
double number(QJsonValue v,double low=0,double high=9007199254740991.0){if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<low||v.toDouble()>high)fail(50301);return v.toDouble();}
qint64 whole(QJsonValue value) { const double n=number(value); if(std::floor(n)!=n) fail(50301); return qint64(n); }
double distanceMeters(double lat1,double lng1,double lat2,double lng2){
    constexpr double radians=0.017453292519943295;
    const double aLat=lat1*radians,bLat=lat2*radians;
    const double dLat=(lat2-lat1)*radians,dLng=(lng2-lng1)*radians;
    const double h=std::sin(dLat/2)*std::sin(dLat/2)+std::cos(aLat)*std::cos(bLat)*std::sin(dLng/2)*std::sin(dLng/2);
    return 12742000.0*std::asin(std::sqrt(qMin(1.0,h)));
}
QJsonObject localNearby(const QJsonObject &data,const QJsonArray &stations){
    QList<QJsonObject> items;const double radius=data["radius_km"].toDouble(100.0)*1000.0;
    for(const auto &value:stations){auto station=value.toObject();const auto distance=distanceMeters(data["latitude"].toDouble(),data["longitude"].toDouble(),station["latitude"].toDouble(),station["longitude"].toDouble());
        if(distance>radius)continue;
        const qint64 meters=qRound64(distance);station["route_distance_meters"]=meters;station["route_duration_seconds"]=qMax<qint64>(60,qRound64(distance/10.0));items<<station;}
    std::sort(items.begin(),items.end(),[](const auto &a,const auto &b){return a["route_distance_meters"].toInteger()<b["route_distance_meters"].toInteger();});
    const qint64 page=data["page"].toInteger(1),size=data["page_size"].toInteger(20),start=(page-1)*size,end=qMin<qint64>(items.size(),start+size);QJsonArray selected;
    for(qint64 i=start;i<end;++i)selected.append(items[i]);
    return {{"items",selected},{"page",page},{"page_size",size},{"total",items.size()}};
}
class MapJob:public QObject {
public:
    MapJob(QNetworkAccessManager &manager,QString key,QUrl base,int timeout,int total,Result done,QObject *parent):QObject(parent),manager(manager),key(key),base(base),timeout(timeout),done(done){deadline.setSingleShot(true);connect(&deadline,&QTimer::timeout,this,[this]{finish({},50301);});deadline.start(total);}
    ~MapJob(){if(reply&&!reply->isFinished())reply->abort();}
    void finish(QJsonValue data,int code=0){if(finished)return;finished=true;deadline.stop();if(reply&&!reply->isFinished())reply->abort();done(data,code);deleteLater();}
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
            if(r->error()!=QNetworkReply::NoError||r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200||!doc.isObject()||doc.object()["status"]!=0){finish({},50301);return;}
            try{callback(doc.object()["result"].toObject());}catch(const Failure&){finish({},50301);}
        });
    }
    void batch(){
        if(offset>=stations.size()){
            if(items.isEmpty()&&!stations.isEmpty()){finish({},50301);return;}
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
    static QString point(QJsonObject d,QString lat,QString lng){return QString::number(d[lat].toDouble(),'f',7)+","+QString::number(d[lng].toDouble(),'f',7);}
    QJsonObject data;QJsonArray stations;QList<QJsonObject> items;int offset=0;
private:
    QNetworkAccessManager &manager;QString key;QUrl base;int timeout;Result done;QTimer deadline;QPointer<QNetworkReply> reply;bool finished=false;
};
}
Maps::Maps(QString key,QObject *parent,QUrl base,int requestTimeout,int totalTimeout):QObject(parent),key(key),base(base),requestTimeout(requestTimeout),totalTimeout(totalTimeout){}
void Maps::run(const QString &a,const QJsonObject &d,const QJsonArray &stations,Result done) {
    if(a=="stations.nearby"&&stations.isEmpty()){done(QJsonObject{{"items",QJsonArray{}},{"page",d["page"].toInteger(1)},{"page_size",d["page_size"].toInteger(20)},{"total",0}},0);return;}
    if(key.trimmed().isEmpty()){if(a=="stations.nearby")done(localNearby(d,stations),0);else done({},50301);return;}
    auto job=new MapJob(network,key,base,requestTimeout,totalTimeout,done,this);job->data=d;job->stations=stations;
    if(a=="stations.nearby"){job->batch();return;}
    if(a=="map.geocode") {
        auto address=d["address"].toString().trimmed();if(address.isEmpty()){job->finish({},40001);return;}
        job->get("/ws/geocoder/v1/",QUrlQuery{{"address",address}},[job,address](auto result){auto loc=result["location"].toObject();job->finish(QJsonObject{{"latitude",number(loc["lat"],-90,90)},{"longitude",number(loc["lng"],-180,180)},{"formatted_address",result["title"].toString(address)}});});return;
    }
    const auto mode=d["mode"].toString("driving");
    job->get("/ws/direction/v1/"+mode,QUrlQuery{{"from",MapJob::point(d,"from_latitude","from_longitude")},{"to",MapJob::point(d,"to_latitude","to_longitude")}},[job,mode](auto result){
        auto routes=result["routes"].toArray();if(routes.isEmpty())fail(50301);auto r=routes[0].toObject();auto poly=r["polyline"].toArray();if(poly.size()<2||poly.size()%2)fail(50301);
        QList<double> coordinates;for(int i=0;i<poly.size();++i){double value=number(poly[i],-9007199254740991.0);if(i>=2)value=coordinates[i-2]+value/1000000.0;if(std::abs(value)>(i%2?180:90))fail(50301);coordinates<<value;}
        QJsonArray points;for(int i=0;i<coordinates.size();i+=2)points.append(QJsonObject{{"latitude",coordinates[i]},{"longitude",coordinates[i+1]}});
        job->finish(QJsonObject{{"mode",mode},{"distance_meters",whole(r["distance"])},{"duration_seconds",qint64(std::round(number(r["duration"],0,10000000)*60))},{"route_points",points}});
    });
}
