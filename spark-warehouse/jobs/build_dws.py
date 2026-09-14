"""Build station, platform, district, pile and quality DWS aggregates."""
from __future__ import annotations

import argparse

from common import batch_table_path, create_spark, validate_batch_id

DWD_TABLES = ("dim_user", "dim_station", "dim_pile", "dwd_charging_order_detail", "dwd_recharge_detail", "dwd_pile_status_event", "dwd_quarantine")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dwd", required=True)
    parser.add_argument("--quality", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--batch-id", required=True)
    parser.add_argument("--master")
    return parser.parse_args()


def build(spark, frames: dict, quality_summary, batch_id: str):
    for name, frame in frames.items():
        frame.createOrReplaceTempView(name)
    quality_summary.createOrReplaceTempView("quality_summary")

    station_day = spark.sql("""
      SELECT o.biz_date, o.station_id, s.station_name, s.district, s.operator_name,
             count(*) AS order_count,
             sum(CASE WHEN o.status='COMPLETED' THEN 1 ELSE 0 END) AS completed_order_count,
             sum(CASE WHEN o.status='CANCELLED' THEN 1 ELSE 0 END) AS cancelled_order_count,
             sum(CASE WHEN o.status='COMPLETED' THEN coalesce(o.energy_wh,0) ELSE 0 END) AS energy_wh,
             sum(CASE WHEN o.status='COMPLETED' THEN coalesce(o.amount_cents,0) ELSE 0 END) AS revenue_cents,
             round(avg(CASE WHEN o.status='COMPLETED' THEN o.amount_cents END),2) AS avg_order_amount_cents,
             round(avg(CASE WHEN o.status IN ('COMPLETED','UNPAID') THEN o.duration_seconds END),2) AS avg_duration_seconds,
             count(DISTINCT CASE WHEN o.status IN ('COMPLETED','UNPAID','CHARGING') THEN o.pile_id END) AS active_pile_count
      FROM dwd_charging_order_detail o JOIN dim_station s ON o.station_id=s.station_id
      WHERE o.biz_date IS NOT NULL GROUP BY o.biz_date,o.station_id,s.station_name,s.district,s.operator_name
    """)
    station_day.createOrReplaceTempView("dws_station_day")

    platform_day = spark.sql("""
      WITH orders AS (
        SELECT biz_date, count(*) order_count, count(DISTINCT user_id) active_user_count,
          sum(CASE WHEN status='COMPLETED' THEN 1 ELSE 0 END) completed_order_count,
          sum(CASE WHEN status='CANCELLED' THEN 1 ELSE 0 END) cancelled_order_count,
          sum(CASE WHEN status='COMPLETED' THEN coalesce(energy_wh,0) ELSE 0 END) energy_wh,
          sum(CASE WHEN status='COMPLETED' THEN coalesce(amount_cents,0) ELSE 0 END) revenue_cents
        FROM dwd_charging_order_detail WHERE biz_date IS NOT NULL GROUP BY biz_date
      ), users AS (
        SELECT to_date(from_utc_timestamp(register_time,'Asia/Shanghai')) biz_date, count(*) new_user_count
        FROM dim_user GROUP BY to_date(from_utc_timestamp(register_time,'Asia/Shanghai'))
      ), recharges AS (
        SELECT biz_date, sum(amount_cents) recharge_cents FROM dwd_recharge_detail GROUP BY biz_date
      )
      SELECT o.biz_date, coalesce(u.new_user_count,0) new_user_count, o.active_user_count,
        o.order_count,o.completed_order_count,o.cancelled_order_count,o.energy_wh,o.revenue_cents,
        coalesce(r.recharge_cents,0) recharge_cents,
        round(o.completed_order_count/o.order_count,4) completion_rate,
        round(o.cancelled_order_count/o.order_count,4) cancellation_rate
      FROM orders o LEFT JOIN users u ON o.biz_date=u.biz_date LEFT JOIN recharges r ON o.biz_date=r.biz_date
    """)

    district_day = spark.sql("""
      SELECT biz_date,district,count(DISTINCT station_id) station_count,sum(order_count) order_count,
        sum(completed_order_count) completed_order_count,sum(energy_wh) energy_wh,sum(revenue_cents) revenue_cents
      FROM dws_station_day GROUP BY biz_date,district
    """)

    pile_day = spark.sql("""
      WITH orders AS (
        SELECT biz_date,pile_id,count(*) charge_count,
          sum(CASE WHEN status IN ('COMPLETED','UNPAID') THEN coalesce(duration_seconds,0) ELSE 0 END)/60.0 charging_minutes,
          sum(CASE WHEN status='COMPLETED' THEN coalesce(energy_wh,0) ELSE 0 END) energy_wh,
          sum(CASE WHEN status='COMPLETED' THEN coalesce(amount_cents,0) ELSE 0 END) revenue_cents
        FROM dwd_charging_order_detail WHERE biz_date IS NOT NULL GROUP BY biz_date,pile_id
      ), faults AS (
        SELECT biz_date,pile_id,sum(CASE WHEN new_status='FAULT' THEN 1 ELSE 0 END) fault_count
        FROM dwd_pile_status_event GROUP BY biz_date,pile_id
      )
      SELECT o.biz_date,o.pile_id,p.station_id,o.charge_count,round(o.charging_minutes,2) charging_minutes,
        o.energy_wh,o.revenue_cents,coalesce(f.fault_count,0) fault_count,
        round(least(o.charging_minutes/1440.0,1.0),4) utilization_rate
      FROM orders o JOIN dim_pile p ON o.pile_id=p.pile_id
      LEFT JOIN faults f ON o.biz_date=f.biz_date AND o.pile_id=f.pile_id
    """)

    quality_batch = spark.sql(f"""
      SELECT '{batch_id}' batch_id,q.table_name,q.rule_id,q.severity,q.total_rows,q.issue_count,q.affected_rows,q.issue_rate,
        coalesce(x.quarantine_count,0) quarantine_count
      FROM quality_summary q LEFT JOIN (
        SELECT table_name,count(*) quarantine_count FROM dwd_quarantine GROUP BY table_name
      ) x ON q.table_name=x.table_name
    """)
    return {"dws_station_day": station_day, "dws_platform_day": platform_day,
            "dws_district_day": district_day, "dws_pile_day": pile_day,
            "dws_data_quality_batch": quality_batch}


def main():
    args = parse_args()
    batch_id = validate_batch_id(args.batch_id)
    spark = create_spark(f"MapForECar-DWS-{batch_id}", args.master)
    try:
        frames = {name: spark.read.parquet(batch_table_path(args.dwd, name, batch_id)) for name in DWD_TABLES}
        quality = spark.read.parquet(batch_table_path(args.quality, "summary", batch_id))
        for name, frame in build(spark, frames, quality, batch_id).items():
            target = batch_table_path(args.output, name, batch_id)
            frame.write.mode("overwrite").parquet(target)
            print(f"{name}: {frame.count()} rows -> {target}")
    finally:
        spark.stop()


if __name__ == "__main__":
    main()
