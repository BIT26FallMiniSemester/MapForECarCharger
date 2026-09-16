"""Build dashboard-facing ADS datasets and export small JSON snapshots."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json

from common import batch_table_path, create_spark, validate_batch_id

DWS_TABLES = ("dws_station_day", "dws_platform_day", "dws_district_day", "dws_pile_day", "dws_data_quality_batch")
DWD_TABLES = ("dim_user", "dim_station", "dim_pile", "dwd_charging_order_detail", "dwd_recharge_detail", "dwd_pile_status_event")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dws", required=True)
    parser.add_argument("--dwd", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--batch-id", required=True)
    parser.add_argument("--master")
    return parser.parse_args()


def build(spark, frames: dict, dwd: dict, batch_id: str):
    from pyspark.sql import functions as F

    for name, frame in {**frames, **dwd}.items():
        frame.createOrReplaceTempView(name)
    max_date = frames["dws_platform_day"].agg(F.max("biz_date")).first()[0]
    if max_date is None:
        raise ValueError("DWS contains no business date")
    data_as_of = str(max_date)
    meta = f"'{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of"

    overview = spark.sql(f"""
      WITH latest AS (SELECT * FROM dws_platform_day WHERE biz_date=date'{data_as_of}'),
      totals AS (SELECT sum(revenue_cents) total_revenue_cents,sum(order_count) total_order_count FROM dws_platform_day),
      pile_stats AS (SELECT count(*) pile_count,sum(CASE WHEN status NOT IN ('FAULT','OFFLINE') THEN 1 ELSE 0 END) online_pile_count,
        sum(CASE WHEN status='IDLE' THEN 1 ELSE 0 END) available_pile_count FROM dim_pile),
      quality AS (SELECT sum(issue_count) issue_count,(SELECT sum(total_rows) FROM
        (SELECT table_name,max(total_rows) total_rows FROM dws_data_quality_batch GROUP BY table_name)) checked_rows
        FROM dws_data_quality_batch)
      SELECT {meta},t.total_revenue_cents,t.total_order_count,l.revenue_cents today_revenue_cents,
        l.order_count today_order_count,l.energy_wh today_energy_wh,(SELECT count(*) FROM dim_station) station_count,
        p.pile_count,p.online_pile_count,p.available_pile_count,p.pile_count-p.online_pile_count unavailable_pile_count,
        round(100.0*p.online_pile_count/p.pile_count,2) online_rate,
        greatest(0.0,round(100.0*(1.0-least(q.issue_count/q.checked_rows,1.0)),2)) quality_score
      FROM totals t CROSS JOIN latest l CROSS JOIN pile_stats p CROSS JOIN quality q
    """)
    trend = spark.sql(f"""SELECT {meta},biz_date,revenue_cents,order_count,completed_order_count,energy_wh
      FROM dws_platform_day WHERE biz_date > date_sub(date'{data_as_of}',30) ORDER BY biz_date""")
    ranking = spark.sql(f"""SELECT {meta},station_id,max(station_name) station_name,max(district) district,
      max(operator_name) operator_name,sum(revenue_cents) revenue_cents,sum(order_count) order_count,sum(energy_wh) energy_wh
      FROM dws_station_day WHERE biz_date > date_sub(date'{data_as_of}',30)
      GROUP BY station_id ORDER BY revenue_cents DESC,station_id LIMIT 20""")
    districts = spark.sql(f"""SELECT {meta},district,max(station_count) station_count,sum(order_count) order_count,
      sum(energy_wh) energy_wh,sum(revenue_cents) revenue_cents FROM dws_district_day
      WHERE biz_date > date_sub(date'{data_as_of}',30) GROUP BY district ORDER BY revenue_cents DESC""")
    station_distribution = spark.sql(f"""
      WITH metrics AS (SELECT station_id,sum(order_count) order_count,sum(energy_wh) energy_wh,
        sum(revenue_cents) revenue_cents FROM dws_station_day
        WHERE biz_date > date_sub(date'{data_as_of}',30) GROUP BY station_id),
      pile_counts AS (SELECT station_id,count(*) pile_count,
        sum(CASE WHEN status='IDLE' THEN 1 ELSE 0 END) available_pile_count,
        sum(CASE WHEN status IN ('RESERVED','CHARGING') THEN 1 ELSE 0 END) busy_pile_count,
        sum(CASE WHEN status='FAULT' THEN 1 ELSE 0 END) fault_count,
        sum(CASE WHEN status='OFFLINE' THEN 1 ELSE 0 END) offline_count FROM dim_pile GROUP BY station_id)
      SELECT {meta},s.station_id,s.station_name,s.address,s.latitude,s.longitude,s.district,s.operator_name,
        s.status station_status,coalesce(m.order_count,0) order_count,coalesce(m.energy_wh,0) energy_wh,
        coalesce(m.revenue_cents,0) revenue_cents,coalesce(p.pile_count,0) pile_count,
        coalesce(p.available_pile_count,0) available_pile_count,coalesce(p.busy_pile_count,0) busy_pile_count,
        coalesce(p.fault_count,0) fault_count,coalesce(p.offline_count,0) offline_count,
        CASE WHEN coalesce(p.pile_count,0)=0 THEN 0 ELSE round(100.0*p.busy_pile_count/p.pile_count,2) END utilization_rate
      FROM dim_station s LEFT JOIN metrics m ON s.station_id=m.station_id LEFT JOIN pile_counts p ON s.station_id=p.station_id
    """)
    pile_status = spark.sql(f"""SELECT {meta},status,count(*) count FROM dim_pile GROUP BY status ORDER BY status""")
    utilization = spark.sql(f"""SELECT {meta},p.pile_id,p.station_id,s.station_name,
      round(avg(p.utilization_rate),4) utilization_rate,sum(p.charge_count) charge_count,sum(p.energy_wh) energy_wh,
      sum(p.revenue_cents) revenue_cents,sum(p.fault_count) fault_count FROM dws_pile_day p
      JOIN dim_station s ON p.station_id=s.station_id WHERE p.biz_date > date_sub(date'{data_as_of}',30)
      GROUP BY p.pile_id,p.station_id,s.station_name ORDER BY utilization_rate DESC,p.pile_id LIMIT 100""")
    quality_overview = spark.sql(f"""WITH totals AS (SELECT table_name,max(total_rows) total_rows,
      max(quarantine_count) quarantine_count FROM dws_data_quality_batch GROUP BY table_name),
      issues AS (SELECT sum(issue_count) issue_count,sum(affected_rows) affected_rows FROM dws_data_quality_batch)
      SELECT {meta},i.issue_count,i.affected_rows,sum(t.quarantine_count) quarantine_count,sum(t.total_rows) checked_rows,
      round(100.0*(1.0-least(i.issue_count/sum(t.total_rows),1.0)),2) quality_score
      FROM issues i CROSS JOIN totals t GROUP BY i.issue_count,i.affected_rows""")
    quality_rules = spark.sql(f"""SELECT {meta},rule_id,max(severity) severity,sum(issue_count) issue_count,
      sum(affected_rows) affected_rows,round(sum(issue_count)/sum(total_rows),6) issue_rate
      FROM dws_data_quality_batch GROUP BY rule_id ORDER BY rule_id""")
    district_charge_type = spark.sql(f"""WITH counts AS (SELECT s.district,p.charge_type,count(*) order_count,
      sum(o.energy_wh) energy_wh,sum(o.amount_cents) revenue_cents FROM dwd_charging_order_detail o
      JOIN dim_station s ON o.station_id=s.station_id JOIN dim_pile p ON o.pile_id=p.pile_id AND o.station_id=p.station_id
      WHERE o.status='COMPLETED' AND o.biz_date > date_sub(date'{data_as_of}',30) GROUP BY s.district,p.charge_type),
      population AS (SELECT district,count(*) station_count FROM dim_station GROUP BY district)
      SELECT {meta},c.*,p.station_count,round(c.order_count/p.station_count,4) orders_per_station
      FROM counts c JOIN population p ON c.district=p.district ORDER BY c.district,c.charge_type""")
    day_type_hour = spark.sql(f"""WITH starts AS (SELECT o.energy_wh,o.amount_cents,
      to_date(from_utc_timestamp(o.started_at,'Asia/Shanghai')) start_date,
      hour(from_utc_timestamp(o.started_at,'Asia/Shanghai')) start_hour FROM dwd_charging_order_detail o
      WHERE o.status='COMPLETED' AND o.started_at IS NOT NULL AND o.biz_date > date_sub(date'{data_as_of}',30)),
      counts AS (SELECT CASE WHEN dayofweek(start_date) IN (1,7) THEN 'WEEKEND' ELSE 'WEEKDAY' END day_type,
      start_hour biz_hour,count(*) order_count,sum(energy_wh) energy_wh,sum(amount_cents) revenue_cents
      FROM starts GROUP BY day_type,start_hour),calendar AS (SELECT CASE WHEN dayofweek(biz_date) IN (1,7)
      THEN 'WEEKEND' ELSE 'WEEKDAY' END day_type,count(*) sample_days FROM
      (SELECT explode(sequence(date_sub(date'{data_as_of}',29),date'{data_as_of}',interval 1 day)) biz_date) GROUP BY day_type)
      SELECT {meta},c.*,d.sample_days,round(c.order_count/d.sample_days,4) avg_orders_per_day
      FROM counts c JOIN calendar d ON c.day_type=d.day_type ORDER BY c.day_type,c.biz_hour""")

    inventory = spark.sql(f"""SELECT {meta},pile_id id,station_id,pile_no,status,rated_power_w
      FROM dim_pile ORDER BY pile_id""")
    realtime = spark.sql(f"""SELECT {meta},o.order_id id,o.order_no,o.status,o.station_id,s.station_name,p.pile_no,
      CASE WHEN o.status='CHARGING' THEN p.rated_power_w ELSE 0 END power_w,coalesce(o.amount_cents,0) amount_cents
      FROM dwd_charging_order_detail o JOIN dim_station s ON o.station_id=s.station_id
      JOIN dim_pile p ON o.pile_id=p.pile_id WHERE o.status IN ('PENDING','RESERVED','CHARGING','UNPAID')
      ORDER BY o.created_at DESC,o.order_id DESC LIMIT 8""")

    def json_rows(frame):
        return [json.loads(value) for value in frame.toJSON().collect()]

    order_summary = json_rows(spark.sql("""SELECT count(*) total_orders,
      coalesce(avg(CASE WHEN status='COMPLETED' THEN duration_seconds END),0) avg_duration_seconds,
      coalesce(avg(CASE WHEN status='COMPLETED' THEN energy_wh END),0) avg_energy_wh,
      coalesce(avg(CASE WHEN status='COMPLETED' THEN amount_cents END),0) avg_amount_cents
      FROM dwd_charging_order_detail"""))[0]
    order_summary["statuses"] = json_rows(spark.sql("SELECT status,count(*) count FROM dwd_charging_order_detail GROUP BY status ORDER BY status"))
    order_summary["funnel"] = json_rows(spark.sql(f"""SELECT count(*) created,
      sum(CASE WHEN reserved_at IS NOT NULL THEN 1 ELSE 0 END) reserved,
      sum(CASE WHEN started_at IS NOT NULL THEN 1 ELSE 0 END) started,
      sum(CASE WHEN stopped_at IS NOT NULL THEN 1 ELSE 0 END) stopped,
      sum(CASE WHEN paid_at IS NOT NULL AND status='COMPLETED' THEN 1 ELSE 0 END) paid
      FROM dwd_charging_order_detail WHERE to_date(from_utc_timestamp(created_at,'Asia/Shanghai'))=date'{data_as_of}'"""))[0]
    order_summary["hourly"] = json_rows(spark.sql(f"""SELECT hour(from_utc_timestamp(created_at,'Asia/Shanghai')) hour,
      count(*) count FROM dwd_charging_order_detail WHERE to_date(from_utc_timestamp(created_at,'Asia/Shanghai'))=date'{data_as_of}'
      GROUP BY hour ORDER BY hour"""))

    user_summary = json_rows(spark.sql(f"""SELECT count(*) total_users,coalesce(sum(balance_cents),0) balance_cents,
      sum(CASE WHEN to_date(from_utc_timestamp(register_time,'Asia/Shanghai'))=date'{data_as_of}' THEN 1 ELSE 0 END) today_new
      FROM dim_user"""))[0]
    user_summary.update(json_rows(spark.sql(f"""SELECT count(distinct user_id) active_30d FROM dwd_charging_order_detail
      WHERE to_date(from_utc_timestamp(created_at,'Asia/Shanghai')) > date_sub(date'{data_as_of}',30)"""))[0])
    user_summary.update(json_rows(spark.sql("SELECT coalesce(sum(amount_cents),0) recharge_cents FROM dwd_recharge_detail"))[0])
    user_summary["statuses"] = json_rows(spark.sql("SELECT status,count(*) count FROM dim_user GROUP BY status ORDER BY status"))
    user_summary["growth"] = json_rows(spark.sql(f"""SELECT cast(to_date(from_utc_timestamp(register_time,'Asia/Shanghai')) as string) date,
      count(*) count FROM dim_user WHERE to_date(from_utc_timestamp(register_time,'Asia/Shanghai')) > date_sub(date'{data_as_of}',30)
      GROUP BY date ORDER BY date"""))
    user_summary["recharges"] = json_rows(spark.sql(f"""SELECT cast(biz_date as string) date,sum(amount_cents) amount_cents
      FROM dwd_recharge_detail WHERE biz_date > date_sub(date'{data_as_of}',30) GROUP BY biz_date ORDER BY biz_date"""))
    user_summary["top"] = json_rows(spark.sql("""SELECT user_id,count(*) order_count,sum(amount_cents) amount_cents
      FROM dwd_charging_order_detail WHERE status='COMPLETED' GROUP BY user_id ORDER BY amount_cents DESC,user_id LIMIT 10"""))
    user_summary["spending"] = json_rows(spark.sql("""WITH spending AS (SELECT u.user_id,coalesce(sum(o.amount_cents),0) cents
      FROM dim_user u LEFT JOIN dwd_charging_order_detail o ON o.user_id=u.user_id AND o.status='COMPLETED' GROUP BY u.user_id),
      buckets AS (SELECT CASE WHEN cents=0 THEN -1 ELSE cast(floor(cents / 2500.0) as int) END bucket FROM spending)
      SELECT CASE WHEN bucket=-1 THEN '未消费' ELSE concat(cast(bucket * 25 as string),'–',cast((bucket + 1) * 25 as string),'元') END band,
      count(*) count FROM buckets GROUP BY bucket ORDER BY bucket"""))
    pile_logs = json_rows(spark.sql("""SELECT l.log_id id,p.pile_no,l.old_status,l.new_status,l.reason,
      cast(l.event_time as string) created_at FROM dwd_pile_status_event l JOIN dim_pile p ON l.pile_id=p.pile_id
      ORDER BY l.log_id DESC LIMIT 20"""))
    energy = json_rows(spark.sql(f"""SELECT coalesce(sum(CASE WHEN status='COMPLETED' AND
      to_date(from_utc_timestamp(paid_at,'Asia/Shanghai'))>=trunc(date'{data_as_of}','MM') THEN amount_cents ELSE 0 END),0) month_revenue_cents,
      coalesce(sum(CASE WHEN status IN ('UNPAID','COMPLETED') AND to_date(from_utc_timestamp(stopped_at,'Asia/Shanghai'))>=trunc(date'{data_as_of}','MM')
      THEN energy_wh ELSE 0 END),0) month_energy_wh FROM dwd_charging_order_detail"""))[0]
    table_counts = [{"name": name, "count": dwd[name].count()} for name in DWD_TABLES]
    generated_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    payload = {"batch_id": batch_id, "generated_at": generated_at, "data_as_of": data_as_of,
      "window_start": str(max_date.replace(day=1)), "source": "Spark DWD / DWS / ADS",
      "orders": order_summary, "users": user_summary, "stations": json_rows(station_distribution),
      "pile_logs": pile_logs, "energy": energy,
      "system": {"batch_id": batch_id, "storage": "Spark ADS", "tables": table_counts, "map_status": "Spark ADS"}}
    topics = spark.createDataFrame([(batch_id, generated_at, data_as_of, json.dumps(payload, ensure_ascii=False))],
      "batch_id string, generated_at string, data_as_of string, payload_json string")

    return {"ads_overview": overview, "ads_revenue_trend_30d": trend,
            "ads_station_ranking_30d": ranking, "ads_district_distribution": districts,
            "ads_station_distribution": station_distribution, "ads_pile_status": pile_status,
            "ads_pile_utilization": utilization, "ads_quality_overview": quality_overview,
            "ads_quality_rules": quality_rules, "ads_district_charge_type_30d": district_charge_type,
            "ads_day_type_hour_30d": day_type_hour, "ads_topics": topics,
            "ads_pile_inventory": inventory, "ads_realtime_orders": realtime}


def main():
    args = parse_args()
    batch_id = validate_batch_id(args.batch_id)
    spark = create_spark(f"MapForECar-ADS-{batch_id}", args.master)
    try:
        frames = {name: spark.read.parquet(batch_table_path(args.dws, name, batch_id)) for name in DWS_TABLES}
        dwd = {name: spark.read.parquet(batch_table_path(args.dwd, name, batch_id)) for name in DWD_TABLES}
        for name, frame in build(spark, frames, dwd, batch_id).items():
            target = batch_table_path(args.output, name, batch_id)
            frame.write.mode("overwrite").parquet(f"{target}/parquet")
            frame.coalesce(1).write.mode("overwrite").json(f"{target}/json")
            print(f"{name}: {frame.count()} rows -> {target}")
    finally:
        spark.stop()


if __name__ == "__main__":
    main()
