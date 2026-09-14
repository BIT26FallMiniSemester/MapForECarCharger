"""Build cleaned DWD dimensions/facts and a traceable quarantine table."""
from __future__ import annotations

import argparse
from functools import reduce

from common import batch_table_path, create_spark, validate_batch_id
from schemas.table_schemas import TABLE_COLUMNS

BLOCKING_RULES = {
    "users": ["DQ001"],
    "stations": ["DQ003"],
    "charging_piles": ["DQ005", "DQ006"],
    "charging_orders": ["DQ008", "DQ009", "DQ010", "DQ011", "DQ012", "DQ013", "DQ017"],
    "recharge_records": ["DQ014"],
    "pile_status_logs": ["DQ015"],
}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ods", required=True)
    parser.add_argument("--quality", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--batch-id", required=True)
    parser.add_argument("--master")
    return parser.parse_args()


def build(frames: dict, issues):
    from pyspark.sql import functions as F
    from pyspark.sql.window import Window

    issue_sets = (issues.groupBy("table_name", "row_id")
                  .agg(F.sort_array(F.collect_set("rule_id")).alias("quality_flags")))

    def tagged(table):
        return (frames[table].join(issue_sets.where(F.col("table_name") == table).drop("table_name"), "row_id", "left")
                .withColumn("quality_flags", F.coalesce("quality_flags", F.array().cast("array<string>"))))

    def has_any(rules):
        return F.exists("quality_flags", lambda value: value.isin(*rules))

    def timestamp(name):
        return F.to_timestamp(F.trim(F.col(name)), "yyyy-MM-dd'T'HH:mm:ssX")

    tagged_frames = {table: tagged(table) for table in TABLE_COLUMNS}
    quarantine_parts = []
    valid = {}
    for table, frame in tagged_frames.items():
        blocked = has_any(BLOCKING_RULES[table])
        quarantine_parts.append(frame.where(blocked).select(
            F.lit(table).alias("table_name"), "row_id", F.to_json(F.struct(*[F.col(c) for c in TABLE_COLUMNS[table]])).alias("raw_record"),
            "quality_flags", F.current_timestamp().alias("quarantined_at")))
        valid[table] = frame.where(~blocked)

    users = (valid["users"]
             .withColumn("_updated", timestamp("updated_at"))
             .withColumn("_rank", F.row_number().over(Window.partitionBy(F.trim("id")).orderBy(F.col("_updated").desc_nulls_last(), F.col("row_id").desc())))
             .where(F.col("_rank") == 1))
    valid["users"] = users
    duplicate_users = tagged_frames["users"].join(users.select("row_id").withColumn("_keep", F.lit(1)), "row_id", "left").where(F.col("_keep").isNull() & ~has_any(BLOCKING_RULES["users"]))
    quarantine_parts.append(duplicate_users.select(F.lit("users").alias("table_name"), "row_id", F.to_json(F.struct(*[F.col(c) for c in TABLE_COLUMNS["users"]])).alias("raw_record"), "quality_flags", F.current_timestamp().alias("quarantined_at")))

    orders = (valid["charging_orders"]
              .withColumn("_updated", timestamp("updated_at"))
              .withColumn("_rank", F.row_number().over(Window.partitionBy(F.trim("order_no")).orderBy(F.col("_updated").desc_nulls_last(), F.col("row_id").desc())))
              .where(F.col("_rank") == 1))
    valid["charging_orders"] = orders
    duplicate_orders = tagged_frames["charging_orders"].join(orders.select("row_id").withColumn("_keep", F.lit(1)), "row_id", "left").where(F.col("_keep").isNull() & ~has_any(BLOCKING_RULES["charging_orders"]))
    quarantine_parts.append(duplicate_orders.select(F.lit("charging_orders").alias("table_name"), "row_id", F.to_json(F.struct(*[F.col(c) for c in TABLE_COLUMNS["charging_orders"]])).alias("raw_record"), "quality_flags", F.current_timestamp().alias("quarantined_at")))

    dwd = {}
    dwd["dim_user"] = valid["users"].select(
        F.col("id").cast("long").alias("user_id"),
        F.regexp_replace(F.trim("phone"), r"^(\d{3})\d{4}(\d{4})$", "$1****$2").alias("masked_phone"),
        F.trim("nickname").alias("nickname"), F.upper(F.trim("status")).alias("status"),
        timestamp("created_at").alias("register_time"), "quality_flags")

    dwd["dim_station"] = valid["stations"].select(
        F.col("id").cast("long").alias("station_id"), F.trim("name").alias("station_name"),
        F.trim("address").alias("address"), F.col("latitude").cast("double").alias("latitude"),
        F.col("longitude").cast("double").alias("longitude"),
        F.when(F.trim("operator_name") == "", "UNKNOWN").otherwise(F.trim("operator_name")).alias("operator_name"),
        F.when(F.trim("district") == "", "未知区域").otherwise(F.trim("district")).alias("district"),
        F.col("price_cents_per_kwh").cast("long").alias("price_cents_per_kwh"),
        F.upper(F.trim("status")).alias("status"), F.trim("service_type").alias("service_type"),
        F.trim("location_type").alias("location_type"), F.col("fast_connector_count").cast("int").alias("fast_connector_count"),
        F.col("slow_connector_count").cast("int").alias("slow_connector_count"), "quality_flags")

    dwd["dim_pile"] = valid["charging_piles"].select(
        F.col("id").cast("long").alias("pile_id"), F.col("station_id").cast("long").alias("station_id"),
        F.trim("pile_no").alias("pile_no"), F.upper(F.trim("charge_type")).alias("charge_type"),
        F.col("rated_power_w").cast("long").alias("rated_power_w"), F.upper(F.trim("status")).alias("status"), "quality_flags")

    order_frame = valid["charging_orders"]
    paid_at = timestamp("paid_at")
    created_at = timestamp("created_at")
    dwd["dwd_charging_order_detail"] = order_frame.select(
        F.col("id").cast("long").alias("order_id"), F.trim("order_no").alias("order_no"),
        F.col("user_id").cast("long").alias("user_id"), F.col("station_id").cast("long").alias("station_id"),
        F.col("pile_id").cast("long").alias("pile_id"), F.upper(F.trim("status")).alias("status"),
        F.col("price_cents_per_kwh").cast("long").alias("price_cents_per_kwh"),
        timestamp("reserved_at").alias("reserved_at"), timestamp("started_at").alias("started_at"),
        timestamp("stopped_at").alias("stopped_at"), F.col("duration_seconds").cast("long").alias("duration_seconds"),
        F.col("energy_wh").cast("long").alias("energy_wh"), F.col("amount_cents").cast("long").alias("amount_cents"),
        paid_at.alias("paid_at"), timestamp("cancelled_at").alias("cancelled_at"), created_at.alias("created_at"),
        F.to_date(F.from_utc_timestamp(F.coalesce(paid_at, created_at), "Asia/Shanghai")).alias("biz_date"),
        F.hour(F.from_utc_timestamp(F.coalesce(paid_at, created_at), "Asia/Shanghai")).alias("biz_hour"), "quality_flags")

    dwd["dwd_recharge_detail"] = valid["recharge_records"].select(
        F.col("id").cast("long").alias("record_id"), F.col("user_id").cast("long").alias("user_id"),
        F.col("amount_cents").cast("long").alias("amount_cents"), F.col("balance_after_cents").cast("long").alias("balance_after_cents"),
        timestamp("created_at").alias("recharge_time"), F.to_date(F.from_utc_timestamp(timestamp("created_at"), "Asia/Shanghai")).alias("biz_date"), "quality_flags")

    dwd["dwd_pile_status_event"] = valid["pile_status_logs"].select(
        F.col("id").cast("long").alias("log_id"), F.col("pile_id").cast("long").alias("pile_id"),
        F.col("order_id").cast("long").alias("order_id"), F.upper(F.trim("old_status")).alias("old_status"),
        F.upper(F.trim("new_status")).alias("new_status"), F.trim("reason").alias("reason"),
        timestamp("created_at").alias("event_time"), F.to_date(F.from_utc_timestamp(timestamp("created_at"), "Asia/Shanghai")).alias("biz_date"), "quality_flags")
    dwd["dwd_quarantine"] = reduce(lambda left, right: left.unionByName(right), quarantine_parts).dropDuplicates(["table_name", "row_id"])
    return dwd


def main():
    args = parse_args()
    batch_id = validate_batch_id(args.batch_id)
    spark = create_spark(f"MapForECar-DWD-{batch_id}", args.master)
    try:
        frames = {table: spark.read.parquet(batch_table_path(args.ods, f"ods_{table}", batch_id)) for table in TABLE_COLUMNS}
        issues = spark.read.parquet(batch_table_path(args.quality, "details", batch_id))
        outputs = build(frames, issues)
        for name, frame in outputs.items():
            target = batch_table_path(args.output, name, batch_id)
            frame.write.mode("overwrite").parquet(target)
            print(f"{name}: {frame.count()} rows -> {target}")
    finally:
        spark.stop()


if __name__ == "__main__":
    main()
