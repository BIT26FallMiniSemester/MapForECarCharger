"""Build dashboard-facing ADS datasets and export small JSON snapshots."""
from __future__ import annotations

import argparse

from common import batch_table_path, create_spark, validate_batch_id

DWS_TABLES = ("dws_station_day", "dws_platform_day", "dws_district_day", "dws_pile_day", "dws_data_quality_batch")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dws", required=True)
    parser.add_argument("--dwd", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--batch-id", required=True)
    parser.add_argument("--master")
    return parser.parse_args()


def build(spark, frames: dict, stations, piles, batch_id: str):
    from pyspark.sql import functions as F

    for name, frame in frames.items():
        frame.createOrReplaceTempView(name)
    stations.createOrReplaceTempView("dim_station")
    piles.createOrReplaceTempView("dim_pile")
    max_date = frames["dws_platform_day"].agg(F.max("biz_date")).first()[0]
    if max_date is None:
        raise ValueError("DWS contains no business date")
    data_as_of = str(max_date)

    overview = spark.sql(f"""
      WITH latest AS (SELECT * FROM dws_platform_day WHERE biz_date=date'{data_as_of}'),
      totals AS (SELECT sum(revenue_cents) total_revenue_cents,sum(order_count) total_order_count FROM dws_platform_day),
      pile_stats AS (SELECT count(*) pile_count,sum(CASE WHEN status NOT IN ('FAULT','OFFLINE') THEN 1 ELSE 0 END) online_pile_count FROM dim_pile),
      quality AS (
        SELECT sum(issue_count) issue_count,
          (SELECT sum(total_rows) FROM (
            SELECT table_name,max(total_rows) total_rows FROM dws_data_quality_batch GROUP BY table_name
          )) checked_rows
        FROM dws_data_quality_batch
      )
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        t.total_revenue_cents,t.total_order_count,l.revenue_cents today_revenue_cents,l.order_count today_order_count,
        l.energy_wh today_energy_wh,(SELECT count(*) FROM dim_station) station_count,p.pile_count,p.online_pile_count,
        p.pile_count-p.online_pile_count unavailable_pile_count,
        round(100.0*p.online_pile_count/p.pile_count,2) online_rate,
        greatest(0.0,round(100.0*(1.0-least(q.issue_count/q.checked_rows,1.0)),2)) quality_score
      FROM totals t CROSS JOIN latest l CROSS JOIN pile_stats p CROSS JOIN quality q
    """)

    trend = spark.sql(f"""
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        biz_date,revenue_cents,order_count,completed_order_count,energy_wh
      FROM dws_platform_day WHERE biz_date > date_sub(date'{data_as_of}',30) ORDER BY biz_date
    """)
    ranking = spark.sql(f"""
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        station_id,max(station_name) station_name,max(district) district,max(operator_name) operator_name,
        sum(revenue_cents) revenue_cents,sum(order_count) order_count,sum(energy_wh) energy_wh
      FROM dws_station_day WHERE biz_date > date_sub(date'{data_as_of}',30)
      GROUP BY station_id ORDER BY revenue_cents DESC,station_id LIMIT 20
    """)
    districts = spark.sql(f"""
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        district,max(station_count) station_count,sum(order_count) order_count,sum(energy_wh) energy_wh,sum(revenue_cents) revenue_cents
      FROM dws_district_day WHERE biz_date > date_sub(date'{data_as_of}',30)
      GROUP BY district ORDER BY revenue_cents DESC
    """)
    station_distribution = spark.sql(f"""
      WITH metrics AS (
        SELECT station_id,sum(order_count) order_count,sum(energy_wh) energy_wh,sum(revenue_cents) revenue_cents,
          sum(active_pile_count) active_pile_days
        FROM dws_station_day WHERE biz_date > date_sub(date'{data_as_of}',30) GROUP BY station_id
      ), pile_counts AS (
        SELECT station_id,count(*) pile_count,sum(CASE WHEN status='IDLE' THEN 1 ELSE 0 END) available_pile_count
        FROM dim_pile GROUP BY station_id
      )
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        s.station_id,s.station_name,s.address,s.latitude,s.longitude,s.district,s.operator_name,
        coalesce(m.order_count,0) order_count,coalesce(m.energy_wh,0) energy_wh,coalesce(m.revenue_cents,0) revenue_cents,
        coalesce(p.pile_count,0) pile_count,coalesce(p.available_pile_count,0) available_pile_count,
        CASE WHEN coalesce(p.pile_count,0)=0 THEN 0 ELSE round(least(m.active_pile_days/(p.pile_count*30.0),1.0),4) END utilization_rate
      FROM dim_station s LEFT JOIN metrics m ON s.station_id=m.station_id LEFT JOIN pile_counts p ON s.station_id=p.station_id
    """)
    pile_status = spark.sql(f"""
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        status,count(*) count FROM dim_pile GROUP BY status ORDER BY status
    """)
    utilization = spark.sql(f"""
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        p.pile_id,p.station_id,s.station_name,round(avg(p.utilization_rate),4) utilization_rate,
        sum(p.charge_count) charge_count,sum(p.energy_wh) energy_wh,sum(p.revenue_cents) revenue_cents,sum(p.fault_count) fault_count
      FROM dws_pile_day p JOIN dim_station s ON p.station_id=s.station_id
      WHERE p.biz_date > date_sub(date'{data_as_of}',30)
      GROUP BY p.pile_id,p.station_id,s.station_name ORDER BY utilization_rate DESC,p.pile_id LIMIT 100
    """)
    quality_overview = spark.sql(f"""
      WITH totals AS (
        SELECT table_name,max(total_rows) total_rows,max(quarantine_count) quarantine_count
        FROM dws_data_quality_batch GROUP BY table_name
      ), issues AS (
        SELECT sum(issue_count) issue_count,sum(affected_rows) affected_rows FROM dws_data_quality_batch
      )
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        i.issue_count,i.affected_rows,sum(t.quarantine_count) quarantine_count,sum(t.total_rows) checked_rows,
        round(100.0*(1.0-least(i.issue_count/sum(t.total_rows),1.0)),2) quality_score
      FROM issues i CROSS JOIN totals t GROUP BY i.issue_count,i.affected_rows
    """)
    quality_rules = spark.sql(f"""
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        rule_id,max(severity) severity,sum(issue_count) issue_count,sum(affected_rows) affected_rows,
        round(sum(issue_count)/sum(total_rows),6) issue_rate
      FROM dws_data_quality_batch GROUP BY rule_id ORDER BY rule_id
    """)
    district_charge_type = spark.sql(f"""
      WITH counts AS (
        SELECT s.district,p.charge_type,count(*) order_count,sum(o.energy_wh) energy_wh,
          sum(o.amount_cents) revenue_cents
        FROM dwd_charging_order_detail o
        JOIN dim_station s ON o.station_id=s.station_id
        JOIN dim_pile p ON o.pile_id=p.pile_id AND o.station_id=p.station_id
        WHERE o.status='COMPLETED' AND o.biz_date > date_sub(date'{data_as_of}',30)
        GROUP BY s.district,p.charge_type
      ), population AS (
        SELECT district,count(*) station_count FROM dim_station GROUP BY district
      )
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        c.*,p.station_count,round(c.order_count/p.station_count,4) orders_per_station
      FROM counts c JOIN population p ON c.district=p.district ORDER BY c.district,c.charge_type
    """)
    day_type_hour = spark.sql(f"""
      WITH starts AS (
        SELECT o.energy_wh,o.amount_cents,
          to_date(from_utc_timestamp(o.started_at,'Asia/Shanghai')) start_date,
          hour(from_utc_timestamp(o.started_at,'Asia/Shanghai')) start_hour
        FROM dwd_charging_order_detail o
        WHERE o.status='COMPLETED' AND o.started_at IS NOT NULL
          AND o.biz_date > date_sub(date'{data_as_of}',30)
      ), counts AS (
        SELECT CASE WHEN dayofweek(start_date) IN (1,7) THEN 'WEEKEND' ELSE 'WEEKDAY' END day_type,
          start_hour biz_hour,count(*) order_count,sum(energy_wh) energy_wh,
          sum(amount_cents) revenue_cents
        FROM starts GROUP BY day_type,start_hour
      ), calendar AS (
        SELECT CASE WHEN dayofweek(biz_date) IN (1,7) THEN 'WEEKEND' ELSE 'WEEKDAY' END day_type,
          count(*) sample_days
        FROM (SELECT explode(sequence(date_sub(date'{data_as_of}',29),date'{data_as_of}',interval 1 day)) biz_date)
        GROUP BY day_type
      )
      SELECT '{batch_id}' batch_id,current_timestamp() generated_at,date'{data_as_of}' data_as_of,
        c.*,d.sample_days,round(c.order_count/d.sample_days,4) avg_orders_per_day
      FROM counts c JOIN calendar d ON c.day_type=d.day_type ORDER BY c.day_type,c.biz_hour
    """)
    return {"ads_overview": overview, "ads_revenue_trend_30d": trend,
            "ads_station_ranking_30d": ranking, "ads_district_distribution": districts,
            "ads_station_distribution": station_distribution, "ads_pile_status": pile_status,
            "ads_pile_utilization": utilization, "ads_quality_overview": quality_overview,
            "ads_quality_rules": quality_rules,
            "ads_district_charge_type_30d": district_charge_type,
            "ads_day_type_hour_30d": day_type_hour}


def main():
    args = parse_args()
    batch_id = validate_batch_id(args.batch_id)
    spark = create_spark(f"MapForECar-ADS-{batch_id}", args.master)
    try:
        frames = {name: spark.read.parquet(batch_table_path(args.dws, name, batch_id)) for name in DWS_TABLES}
        stations = spark.read.parquet(batch_table_path(args.dwd, "dim_station", batch_id))
        piles = spark.read.parquet(batch_table_path(args.dwd, "dim_pile", batch_id))
        orders = spark.read.parquet(batch_table_path(args.dwd, "dwd_charging_order_detail", batch_id))
        orders.createOrReplaceTempView("dwd_charging_order_detail")
        for name, frame in build(spark, frames, stations, piles, batch_id).items():
            target = batch_table_path(args.output, name, batch_id)
            frame.write.mode("overwrite").parquet(f"{target}/parquet")
            frame.coalesce(1).write.mode("overwrite").json(f"{target}/json")
            print(f"{name}: {frame.count()} rows -> {target}")
    finally:
        spark.stop()


if __name__ == "__main__":
    main()
