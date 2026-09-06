#include "business.h"
#include <QTimeZone>

namespace {
QString text(const QJsonObject &d,const QString &key) { auto s=d[key].toString().trimmed(); if(s.isEmpty()) fail(40001); return s; }
qint64 idOf(const QJsonObject &d,const QString &key) { return integer(d[key]); }
QStringList states(){return {"IDLE","RESERVED","CHARGING","FAULT","OFFLINE"};}
QString activeSql(){return "('PENDING','RESERVED','CHARGING','UNPAID')";}
QDateTime date(const QJsonValue &v){return QDateTime::fromString(v.toString(),Qt::ISODateWithMs);}
}
Identity Business::authorize(const QString &action,const QString &token) {
    const auto spec=contract()["x-actions"].toObject()[action].toObject();
    if(spec.isEmpty()) fail(40009);
    if(spec["role"]=="NONE") return {};
    auto it=sessions.find(token);
    if(it==sessions.end()) fail(40101);
    if(it->expires<=QDateTime::currentDateTimeUtc()){sessions.erase(it);fail(40101);}
    const auto identity=it.value();
    if(spec["role"]!="EITHER"&&spec["role"].toString()!=identity.role) fail(40301);
    const auto record=db.one(identity.role=="ADMIN"?"SELECT status FROM admins WHERE id=?":"SELECT status FROM users WHERE id=?",{identity.id});
    if(record.isEmpty()) fail(40101);
    if(record["status"]!="NORMAL") fail(40301);
    return identity;
}
QJsonObject Business::user(qint64 id) {
    auto o=db.one("SELECT u.id,u.phone,u.nickname,u.avatar_id,u.balance_cents,u.status,u.created_at,"
                  "(SELECT count(*) FROM charging_orders o WHERE o.user_id=u.id) order_count,"
                  "(SELECT coalesce(sum(o.amount_cents),0) FROM charging_orders o WHERE o.user_id=u.id AND o.status='COMPLETED') total_spent_cents "
                  "FROM users u WHERE u.id=?",{id}); if(o.isEmpty()) fail(40401); return o;
}
QJsonObject Business::pile(qint64 id) {
    auto o=db.one("SELECT p.id,p.station_id,s.name station_name,p.pile_no,p.charge_type,p.rated_power_w,p.status,"
                  "(SELECT count(*) FROM charging_orders o WHERE o.pile_id=p.id AND o.status='COMPLETED') total_charge_count,"
                  "(SELECT coalesce(sum(o.duration_seconds),0) FROM charging_orders o WHERE o.pile_id=p.id AND o.status='COMPLETED') total_charge_duration_seconds "
                  "FROM charging_piles p JOIN stations s ON s.id=p.station_id WHERE p.id=?",{id}); if(o.isEmpty()) fail(40401); return o;
}
QJsonObject Business::station(qint64 id) {
    auto o=db.one("SELECT * FROM stations WHERE id=?",{id}); if(o.isEmpty()) fail(40401);
    const auto stats=db.one("SELECT count(*) total_piles,coalesce(sum(status='IDLE'),0) available_piles,coalesce(sum(status!='OFFLINE'),0) online_piles FROM charging_piles WHERE station_id=?",{id});
    o["total_piles"]=stats["total_piles"];o["available_piles"]=stats["available_piles"];o["online_piles"]=stats["online_piles"];return o;
}
QJsonArray Business::nearbyCandidates() {
    QJsonArray out;for(auto o:db.rows("SELECT id FROM stations s WHERE status='ACTIVE' AND price_cents_per_kwh IS NOT NULL AND EXISTS(SELECT 1 FROM charging_piles p WHERE p.station_id=s.id) ORDER BY id"))out.append(station(o.toObject()["id"].toInteger()));return out;
}
QJsonObject Business::order(qint64 id) {
    auto o=db.one("SELECT * FROM charging_orders WHERE id=?",{id});if(o.isEmpty())fail(40401);
    auto s=db.one("SELECT id,name FROM stations WHERE id=?",{o["station_id"].toInteger()});
    auto p=db.one("SELECT id,pile_no,rated_power_w FROM charging_piles WHERE id=?",{o["pile_id"].toInteger()});
    o.remove("user_id");o.remove("station_id");o.remove("pile_id");o["station"]=s;o["pile"]=p;
    bool estimated=o["status"]=="CHARGING";o["estimated"]=estimated;
    if(estimated) {
        if(!date(o["started_at"]).isValid()) fail(50000);
        const auto seconds=qMax<qint64>(0,date(o["started_at"]).msecsTo(QDateTime::currentDateTimeUtc())/1000);
        const auto energy=roundedProduct(p["rated_power_w"].toInteger(),seconds,3600);
        o["duration_seconds"]=seconds;o["energy_wh"]=energy;o["amount_cents"]=roundedProduct(energy,o["price_cents_per_kwh"].toInteger(),1000);
    }return o;
}
QJsonObject Business::listing(const QString &sql,const QVariantList &args,const QJsonObject &d,const std::function<QJsonObject(QJsonObject)> &transform) {
    const qint64 page=d["page"].toInteger(1), size=d["page_size"].toInteger(20);
    const auto total=db.scalar("SELECT count(*) FROM ("+sql+")",args);
    auto bindings=args;bindings<<size<<(page-1)*size;
    auto items=db.rows(sql+" LIMIT ? OFFSET ?",bindings);
    if(transform){QJsonArray mapped;for(auto item:items)mapped.append(transform(item.toObject()));items=mapped;}
    return {{"items",items},{"page",page},{"page_size",size},{"total",total}};
}
void Business::pileLog(qint64 p,qint64 o,const QString &before,const QString &after,const QString &reason) {
    db.execute("INSERT INTO pile_status_logs(pile_id,order_id,old_status,new_status,reason,created_at) VALUES(?,?,?,?,?,?)",{p,o?QVariant(o):QVariant(),before,after,reason,utcNow()});
}
void Business::operationLog(qint64 admin,const QString &action,const QString &type,qint64 target,const QJsonObject &data) {
    db.execute("INSERT INTO operation_logs(admin_id,action,target_type,target_id,detail_json,created_at) VALUES(?,?,?,?,?,?)",{admin,action,type,target,QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)),utcNow()});
}
void Business::expire() {
    Transaction tx(db);
    for(auto item:db.rows("SELECT id,pile_id FROM charging_orders WHERE status='RESERVED' AND expires_at<=?",{utcNow()})) {
        auto o=item.toObject();auto id=o["id"].toInteger(),p=o["pile_id"].toInteger();
        if(db.execute("UPDATE charging_piles SET status='IDLE',reserved_order_id=NULL,updated_at=? WHERE id=? AND status='RESERVED' AND reserved_order_id=?",{utcNow(),p,id})!=1)fail(50001);
        db.execute("UPDATE charging_orders SET status='CANCELLED',cancelled_at=?,updated_at=? WHERE id=? AND status='RESERVED'",{utcNow(),utcNow(),id});
        pileLog(p,id,"RESERVED","IDLE","EXPIRE");
    }tx.commit();
    const auto now=QDateTime::currentDateTimeUtc();
    for(auto it=sessions.begin();it!=sessions.end();) if(it->expires<=now)it=sessions.erase(it);else ++it;
}
QJsonValue Business::dispatch(const QString &a,const QJsonObject &d,const Identity &who) {
    if(a=="system.health"){db.scalar("SELECT 1");return QJsonObject{{"status","ok"},{"database","ok"}};}
    if(a=="auth.user.login"||a=="auth.admin.login") {
        bool isUser=a=="auth.user.login",isNew=false;QJsonObject account;
        if(isUser) {
            auto phone=text(d,"phone");
            account=db.one("SELECT id,status FROM users WHERE phone=?",{phone});
            if(account.isEmpty()) {
                auto id=db.insert("INSERT INTO users(phone,nickname,created_at,updated_at) VALUES(?,?,?,?)",{phone,QStringLiteral("用户")+phone.right(4),utcNow(),utcNow()});
                account=user(id);isNew=true;
            }
        }else {
            account=db.one("SELECT id,username,password_hash,display_name,status FROM admins WHERE username=?",{text(d,"username")});
            if(account.isEmpty()||!passwordVerify(d["password"].toString(),account["password_hash"].toString()))fail(40101);
        }
        if(account["status"]!="NORMAL")fail(40301);
        qint64 id=account["id"].toInteger();QString token=randomToken();int seconds=isUser?86400:28800;
        sessions.insert(token,{id,isUser?"USER":"ADMIN",QDateTime::currentDateTimeUtc().addSecs(seconds)});
        QJsonObject result{{"access_token",token},{"expires_in",seconds}};
        if(isUser){result["is_new_user"]=isNew;result["user"]=user(id);}
        else {account.remove("password_hash");result["admin"]=account;db.execute("UPDATE admins SET last_login_at=?,updated_at=? WHERE id=?",{utcNow(),utcNow(),id});}
        return result;
    }
    if(a=="users.me.get")return user(who.id);
    if(a=="users.me.update"){db.execute("UPDATE users SET nickname=?,updated_at=? WHERE id=?",{text(d,"nickname"),utcNow(),who.id});return user(who.id);}
    if(a.startsWith("users.avatar.")) {
        const auto directory=QFileInfo(db.path()).absolutePath()+"/avatars";
        if(a=="users.avatar.get") {
            if(user(who.id)["avatar_id"]!=d["avatar_id"])fail(40301);
            auto id=text(d,"avatar_id");QFile file(directory+"/"+id);
            if(!file.open(QIODevice::ReadOnly))fail(40401);
            return QJsonObject{{"content_type",id.endsWith(".png")?"image/png":"image/jpeg"},{"content_base64",QString::fromLatin1(file.readAll().toBase64())}};
        }
        const auto decoded=QByteArray::fromBase64Encoding(d["content_base64"].toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
        if(!decoded||decoded.decoded.isEmpty()||decoded.decoded.size()>262144)fail(40001);
        bool png=d["content_type"]=="image/png";
        if(png?!decoded.decoded.startsWith(QByteArray::fromHex("89504e470d0a1a0a")):!decoded.decoded.startsWith(QByteArray::fromHex("ffd8ff")))fail(40001);
        QDir().mkpath(directory);QString id=randomToken().left(32)+(png?".png":".jpg");QSaveFile file(directory+"/"+id);
        if(!file.open(QIODevice::WriteOnly)||file.write(decoded.decoded)!=decoded.decoded.size()||!file.commit())fail(50000);
        try{db.execute("UPDATE users SET avatar_id=?,updated_at=? WHERE id=?",{id,utcNow(),who.id});}catch(...){QFile::remove(directory+"/"+id);throw;}
        return QJsonObject{{"avatar_id",id}};
    }
    const QString rechargeSelect="SELECT id record_id,amount_cents,balance_after_cents balance_cents,created_at FROM recharge_records";
    if(a=="wallet.recharges.list")return listing(rechargeSelect+" WHERE user_id=? ORDER BY id DESC",{who.id},d);
    if(a=="wallet.recharges.create") {
        const auto request=text(d,"client_request_id");const auto amount=idOf(d,"amount_cents");Transaction tx(db);
        auto previous=db.one("SELECT user_id,amount_cents,id FROM recharge_records WHERE client_request_id=?",{request});
        qint64 record=previous.value("id").toInteger();
        if(!previous.isEmpty()){if(previous["user_id"].toInteger()!=who.id)fail(40301);if(previous["amount_cents"].toInteger()!=amount)fail(40001);}
        else {
            if(db.execute("UPDATE users SET balance_cents=balance_cents+?,updated_at=? WHERE id=? AND balance_cents<=?",{amount,utcNow(),who.id,9007199254740991LL-amount})!=1)fail(40001);
            record=db.insert("INSERT INTO recharge_records(user_id,client_request_id,amount_cents,balance_after_cents,created_at) VALUES(?,?,?,?,?)",{who.id,request,amount,user(who.id)["balance_cents"].toInteger(),utcNow()});
        }tx.commit();return db.one(rechargeSelect+" WHERE id=?",{record});
    }
    if(a.startsWith("orders."))return orderAction(a,d,who.id);
    if(a.startsWith("admin."))return adminAction(a,d,who.id);
    if(a=="stations.detail")return station(idOf(d,"station_id"));
    if(a=="piles.detail")return pile(idOf(d,"pile_id"));
    if(a=="stations.list") {
        QString sql="SELECT id FROM stations WHERE status='ACTIVE'";QVariantList args;
        if(d.contains("district")){sql+=" AND district=?";args<<d["district"].toString();}
        if(d.contains("keyword")){sql+=" AND name LIKE ?";args<<"%"+d["keyword"].toString()+"%";}
        return listing(sql+" ORDER BY id",args,d,[this](auto o){return station(o["id"].toInteger());});
    }
    if(a=="stations.piles.list") {
        auto id=idOf(d,"station_id");station(id);QString sql="SELECT id FROM charging_piles WHERE station_id=?";QVariantList args{id};
        if(d.contains("status")){sql+=" AND status=?";args<<d["status"].toString();}
        return listing(sql+" ORDER BY id",args,d,[this](auto o){return pile(o["id"].toInteger());});
    }fail(40009);
}
QJsonValue Business::orderAction(const QString &a,const QJsonObject &d,qint64 userId) {
    expire();
    if(a=="orders.list") {
        QString sql="SELECT id FROM charging_orders WHERE user_id=?";QVariantList args{userId};
        if(d.contains("status")){sql+=" AND status=?";args<<d["status"].toString();}
        return listing(sql+" ORDER BY id DESC",args,d,[this](auto o){return order(o["id"].toInteger());});
    }
    if(a=="orders.active") {
        auto o=db.one("SELECT id FROM charging_orders WHERE user_id=? AND status IN "+activeSql(),{userId});
        return o.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(order(o["id"].toInteger()));
    }
    if(a=="orders.create") {
        Transaction tx(db);const auto stationId=idOf(d,"station_id"),pileId=idOf(d,"pile_id");auto s=station(stationId),p=pile(pileId);
        if(p["station_id"].toInteger()!=stationId)fail(40001);
        if(s["status"]!="ACTIVE"||s["price_cents_per_kwh"].isNull())fail(40001);
        if(db.scalar("SELECT count(*) FROM charging_orders WHERE user_id=? AND status IN "+activeSql(),{userId}))fail(40002);
        auto id=db.insert("INSERT INTO charging_orders(order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,created_at,updated_at) VALUES(?,?,?,?,'PENDING',?,?,?)",{"C"+randomToken().left(30),userId,stationId,pileId,s["price_cents_per_kwh"].toInteger(),utcNow(),utcNow()});
        tx.commit();return order(id);
    }
    const auto id=idOf(d,"order_id");auto o=db.one("SELECT * FROM charging_orders WHERE id=?",{id});
    if(o.isEmpty()) fail(40401);
    if(o["user_id"].toInteger()!=userId) fail(40301);
    if(a=="orders.detail")return order(id);
    const QString status=o["status"].toString();const qint64 pileId=o["pile_id"].toInteger();
    if(a=="orders.stop"&&(status=="UNPAID"||status=="COMPLETED"))return order(id);
    if(a=="orders.settle"&&status=="COMPLETED")return QJsonObject{{"order",order(id)},{"balance_cents",user(userId)["balance_cents"]}};
    if(a=="orders.start"&&status=="CANCELLED"&&db.scalar("SELECT count(*) FROM pile_status_logs WHERE order_id=? AND reason='EXPIRE'",{id}))fail(40005);
    Transaction tx(db);const auto now=utcNow();
    if(a=="orders.reserve") {
        if(status!="PENDING")fail(40004);
        if(station(o["station_id"].toInteger())["status"]!="ACTIVE")fail(40003);
        if(db.execute("UPDATE charging_piles SET status='RESERVED',reserved_order_id=?,updated_at=? WHERE id=? AND status='IDLE' AND reserved_order_id IS NULL",{id,now,pileId})!=1)fail(40003);
        db.execute("UPDATE charging_orders SET status='RESERVED',reserved_at=?,expires_at=?,updated_at=? WHERE id=?",{now,QDateTime::currentDateTimeUtc().addSecs(900).toString(Qt::ISODateWithMs),now,id});pileLog(pileId,id,"IDLE","RESERVED","RESERVE");
    }else if(a=="orders.start") {
        if(status!="RESERVED")fail(40004);
        if(db.execute("UPDATE charging_piles SET status='CHARGING',updated_at=? WHERE id=? AND status='RESERVED' AND reserved_order_id=?",{now,pileId,id})!=1)fail(40004);
        db.execute("UPDATE charging_orders SET status='CHARGING',started_at=?,updated_at=? WHERE id=?",{now,now,id});pileLog(pileId,id,"RESERVED","CHARGING","START");
    }else if(a=="orders.stop") {
        if(status!="CHARGING") fail(40004);
        auto calculated=order(id);
        if(db.execute("UPDATE charging_piles SET status='IDLE',reserved_order_id=NULL,updated_at=? WHERE id=? AND status='CHARGING' AND reserved_order_id=?",{now,pileId,id})!=1)fail(40004);
        db.execute("UPDATE charging_orders SET status='UNPAID',stopped_at=?,duration_seconds=?,energy_wh=?,amount_cents=?,updated_at=? WHERE id=?",{now,calculated["duration_seconds"].toInteger(),calculated["energy_wh"].toInteger(),calculated["amount_cents"].toInteger(),now,id});pileLog(pileId,id,"CHARGING","IDLE","STOP");
    }else if(a=="orders.settle") {
        if(status!="UNPAID") fail(40004);
        const auto amount=o["amount_cents"].toInteger();
        if(db.execute("UPDATE users SET balance_cents=balance_cents-?,updated_at=? WHERE id=? AND balance_cents>=?",{amount,now,userId,amount})!=1)fail(40006);
        db.execute("UPDATE charging_orders SET status='COMPLETED',paid_at=?,updated_at=? WHERE id=? AND status='UNPAID'",{now,now,id});
    }else if(a=="orders.cancel") {
        if(status!="PENDING"&&status!="RESERVED")fail(40004);
        if(status=="RESERVED") {
            if(db.execute("UPDATE charging_piles SET status='IDLE',reserved_order_id=NULL,updated_at=? WHERE id=? AND status='RESERVED' AND reserved_order_id=?",{now,pileId,id})!=1)fail(40004);
            pileLog(pileId,id,"RESERVED","IDLE","CANCEL");
        }db.execute("UPDATE charging_orders SET status='CANCELLED',cancelled_at=?,updated_at=? WHERE id=?",{now,now,id});
    }else fail(40009);
    tx.commit();if(a=="orders.settle")return QJsonObject{{"order",order(id)},{"balance_cents",user(userId)["balance_cents"]}};return order(id);
}
QJsonValue Business::adminAction(const QString &a,const QJsonObject &d,qint64 adminId) {
    if(a=="admin.pile_status") {QJsonObject out;for(const auto &s:states())out[s]=db.scalar("SELECT count(*) FROM charging_piles WHERE status=?",{s});return out;}
    const QTimeZone zone("Asia/Shanghai");const auto today=QDateTime::currentDateTimeUtc().toTimeZone(zone).date();
    auto boundary=[&](QDate day){return QDateTime(day,QTime(0,0),zone).toUTC().toString(Qt::ISODateWithMs);};
    auto revenue=[&](QString from,QString to){return db.scalar("SELECT coalesce(sum(amount_cents),0) FROM charging_orders WHERE status='COMPLETED' AND paid_at>=? AND paid_at<?",{from,to});};
    auto count=[&](QString from,QString to){return db.scalar("SELECT count(*) FROM charging_orders WHERE created_at>=? AND created_at<?",{from,to});};
    if(a=="admin.overview")return QJsonObject{{"today_revenue_cents",revenue(boundary(today),boundary(today.addDays(1)))},{"month_revenue_cents",revenue(boundary(QDate(today.year(),today.month(),1)),boundary(QDate(today.year(),today.month(),1).addMonths(1)))},{"total_revenue_cents",db.scalar("SELECT coalesce(sum(amount_cents),0) FROM charging_orders WHERE status='COMPLETED'")},{"today_order_count",count(boundary(today),boundary(today.addDays(1)))},{"today_energy_wh",db.scalar("SELECT coalesce(sum(energy_wh),0) FROM charging_orders WHERE status IN ('UNPAID','COMPLETED') AND stopped_at>=? AND stopped_at<?",{boundary(today),boundary(today.addDays(1))})},{"total_energy_wh",db.scalar("SELECT coalesce(sum(energy_wh),0) FROM charging_orders WHERE status IN ('UNPAID','COMPLETED')")}};
    if(a=="admin.revenue_trend") {int days=d["days"].toInt(7);QJsonArray items;for(int i=days-1;i>=0;--i){auto day=today.addDays(-i);items.append(QJsonObject{{"date",day.toString(Qt::ISODate)},{"revenue_cents",revenue(boundary(day),boundary(day.addDays(1)))},{"order_count",count(boundary(day),boundary(day.addDays(1)))}});}return QJsonObject{{"days",days},{"items",items}};}
    if(a=="admin.users.list"||a=="admin.stations.list"||a=="admin.piles.list") {
        const auto type=a.section('.',1,1);const auto table=type=="piles"?"charging_piles":type;QString sql="SELECT id FROM "+table+" WHERE 1=1";QVariantList args;
        for(const auto &field:QStringList{"status","district","station_id","charge_type"})if(d.contains(field)){sql+=" AND "+field+"=?";args<<d[field].toVariant();}
        if(d.contains("keyword")){const auto term="%"+d["keyword"].toString()+"%";if(type=="users"){sql+=" AND (phone LIKE ? OR nickname LIKE ?)";args<<term<<term;}else if(type=="piles"){sql+=" AND pile_no LIKE ?";args<<term;}else{sql+=" AND name LIKE ?";args<<term;}}
        return listing(sql+" ORDER BY id",args,d,[&](auto o){auto id=o["id"].toInteger();return type=="users"?user(id):type=="stations"?station(id):pile(id);});
    }
    if(a=="admin.users.detail") {
        auto id=idOf(d,"user_id");auto out=user(id);out["recharge_total_cents"]=db.scalar("SELECT coalesce(sum(amount_cents),0) FROM recharge_records WHERE user_id=?",{id});
        out["recent_orders"]=db.rows("SELECT order_no,status,amount_cents,created_at FROM charging_orders WHERE user_id=? ORDER BY id DESC LIMIT 5",{id});
        out["recent_recharge_records"]=db.rows("SELECT amount_cents,created_at FROM recharge_records WHERE user_id=? ORDER BY id DESC LIMIT 5",{id});return out;
    }
    if(a=="admin.piles.detail") {auto id=idOf(d,"pile_id");auto out=pile(id);out["status_logs"]=db.rows("SELECT * FROM pile_status_logs WHERE pile_id=? ORDER BY id DESC LIMIT 100",{id});return out;}
    Transaction tx(db);
    if(a=="admin.users.freeze"||a=="admin.users.unfreeze") {
        auto id=idOf(d,"user_id");auto u=user(id);QString desired=a.endsWith(".freeze")?"FROZEN":"NORMAL";
        if(desired=="FROZEN"&&db.scalar("SELECT count(*) FROM charging_orders WHERE user_id=? AND status IN "+activeSql(),{id}))fail(40007);
        if(u["status"]!=desired){db.execute("UPDATE users SET status=?,updated_at=? WHERE id=?",{desired,utcNow(),id});operationLog(adminId,a,"USER",id,d);}tx.commit();return user(id);
    }
    if(a=="admin.stations.create") {
        auto id=db.insert("INSERT INTO stations(name,address,latitude,longitude,price_cents_per_kwh,operator_name,district,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?)",{text(d,"name"),text(d,"address"),d["latitude"].toDouble(),d["longitude"].toDouble(),idOf(d,"price_cents_per_kwh"),d["operator_name"].toVariant(),d["district"].toVariant(),utcNow(),utcNow()});operationLog(adminId,a,"STATION",id,d);tx.commit();return station(id);
    }
    if(a=="admin.stations.update") {
        const auto id=idOf(d,"station_id");station(id);QStringList sets;QVariantList args;
        for(auto it=d.begin();it!=d.end();++it)if(it.key()!="station_id") {sets<<it.key()+"=?";args<<(it.value().isString()?QVariant(text(d,it.key())):it.value().toVariant());}
        if(sets.isEmpty()) fail(40001);
        sets<<"updated_at=?";args<<utcNow()<<id;
        db.execute("UPDATE stations SET "+sets.join(',')+" WHERE id=?",args);operationLog(adminId,a,"STATION",id,d);tx.commit();return station(id);
    }
    if(a=="admin.piles.create") {
        auto stationId=idOf(d,"station_id");station(stationId);auto number=text(d,"pile_no");
        if(db.scalar("SELECT count(*) FROM charging_piles WHERE pile_no=?",{number}))fail(40008);
        auto id=db.insert("INSERT INTO charging_piles(station_id,pile_no,charge_type,rated_power_w,created_at,updated_at) VALUES(?,?,?,?,?,?)",{stationId,number,text(d,"charge_type"),idOf(d,"rated_power_w"),utcNow(),utcNow()});operationLog(adminId,a,"PILE",id,d);tx.commit();return pile(id);
    }
    if(a=="admin.piles.recover") {
        auto id=idOf(d,"pile_id");pile(id);
        if(db.execute("UPDATE charging_piles SET status='IDLE',updated_at=? WHERE id=? AND status='FAULT' AND reserved_order_id IS NULL",{utcNow(),id})!=1)fail(40004);
        pileLog(id,0,"FAULT","IDLE","ADMIN_RECOVER");operationLog(adminId,a,"PILE",id,d);tx.commit();return pile(id);
    }fail(40009);
}
