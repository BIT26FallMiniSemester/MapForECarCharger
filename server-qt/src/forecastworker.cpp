#include "forecastworker.h"

#include <QDateTime>
#include <QJsonArray>
#include <QMap>
#include <QtMath>

void ForecastWorker::calculate(const QJsonObject &input)
{
    struct Average { double orders=0; double energy=0; };
    QMap<int,Average> profile;
    double allOrders=0,allEnergy=0,allDays=0;
    for(const auto &value:input["hourly_profile"].toArray()) {
        const auto row=value.toObject();const double days=qMax(1,row["days"].toInt());
        const int key=row["weekday"].toInt()*24+row["hour"].toInt();
        profile[key]={row["orders"].toDouble()/days,row["energy_wh"].toDouble()/days};
        allOrders+=row["orders"].toDouble();allEnergy+=row["energy_wh"].toDouble();allDays+=days;
    }
    const Average fallback{allDays?allOrders/allDays/24.0:0,allDays?allEnergy/allDays/24.0:0};
    const int total=qMax(1,input["pile_count"].toInt());const int busy=input["busy_piles"].toInt();
    QJsonArray points;QList<double> orderValues,energyValues;double maxUtil=0;QString peak;
    const auto current=QDateTime::currentDateTime();
    const auto now=current.addSecs(3600-current.time().minute()*60-current.time().second());
    for(int i=0;i<24;++i) {
        const auto at=now.addSecs(i*3600);const int key=int(at.date().dayOfWeek()%7)*24+at.time().hour();
        const auto avg=profile.value(key,fallback);const double utilization=qMin(100.0,100.0*(busy+avg.orders)/total);
        orderValues.append(avg.orders);energyValues.append(avg.energy/1000.0);
        points.append(QJsonObject{{"time",at.toString("MM-dd HH:00")},{"orders",qRound(avg.orders*100)/100.0},{"energy_kwh",qRound(avg.energy/10.0)/100.0},{"utilization",qRound(utilization*10)/10.0}});
        if(utilization>maxUtil){maxUtil=utilization;peak=at.toString("MM-dd HH:00");}
    }
    QJsonArray horizons;
    for(const int hours:{1,6,24}) {double orders=0,energy=0;for(int i=0;i<hours;++i){orders+=orderValues[i];energy+=energyValues[i];}horizons.append(QJsonObject{{"hours",hours},{"orders",qRound(orders*100)/100.0},{"energy_kwh",qRound(energy*100)/100.0}});}
    QJsonArray warnings;if(maxUtil>=80)warnings.append(QStringLiteral("%1 预计负载率 %2%，请关注可用电桩。").arg(peak).arg(maxUtil,0,'f',1));else warnings.append(QStringLiteral("未来 24 小时无高负载预警"));
    QJsonObject result=input;result["forecast_points"]=points;result["forecast_horizons"]=horizons;result["warnings"]=warnings;result["model"]=QStringLiteral("按星期与小时分组的历史均值时序模型");emit ready(result);
}
