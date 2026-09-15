"""Detect DQ001-DQ018 from ODS data without consulting injection metadata."""
from __future__ import annotations

import argparse
from functools import reduce

from common import batch_table_path, create_spark, parse_timestamp, validate_batch_id
from schemas.table_schemas import TABLE_COLUMNS


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ods", required=True, help="ODS root")
    parser.add_argument("--output", required=True, help="Quality report root")
    parser.add_argument("--batch-id", required=True)
    parser.add_argument("--master", help="Spark master override, for example local[*]")
    return parser.parse_args()


def detect(frames: dict, batch_id: str):
    from pyspark.sql import functions as F
    from pyspark.sql.window import Window

    issue_frames = []

    def blank(name):
        return F.coalesce(F.trim(F.col(name)), F.lit("")) == ""

    def add(rule_id, table, condition, reason, severity="ERROR"):
        issue_frames.append(frames[table].where(condition).select(
            F.lit(batch_id).alias("batch_id"), F.lit(table).alias("table_name"),
            "row_id", F.lit(rule_id).alias("rule_id"), F.lit(severity).alias("severity"),
            F.lit(reason).alias("reason"), F.current_timestamp().alias("detected_at")))

    users = frames["users"]
    add("DQ001", "users", ~F.coalesce(F.trim("phone"), F.lit("")).rlike(r"^1[3-9][0-9]{9}$"), "phone is blank or invalid")
    user_dupes = (users.withColumn("id_count", F.count("*").over(Window.partitionBy(F.trim("id"))))
                  .withColumn("phone_count", F.count("*").over(Window.partitionBy(F.trim("phone")))))
    issue_frames.append(user_dupes.where((F.col("id_count") > 1) | (F.col("phone_count") > 1)).select(
        F.lit(batch_id).alias("batch_id"), F.lit("users").alias("table_name"), "row_id",
        F.lit("DQ002").alias("rule_id"), F.lit("ERROR").alias("severity"),
        F.lit("duplicate id or phone").alias("reason"), F.current_timestamp().alias("detected_at")))

    stations = frames["stations"]
    lat, lon = F.col("latitude").cast("double"), F.col("longitude").cast("double")
    add("DQ003", "stations", lat.isNull() | lon.isNull() | ~lat.between(-90, 90) | ~lon.between(-180, 180), "coordinate is missing or outside valid range")
    add("DQ004", "stations", blank("district") | blank("operator_name") | (F.col("district") != F.trim("district")) | (F.col("operator_name") != F.trim("operator_name")), "dimension text is blank or untrimmed", "WARN")

    piles = frames["charging_piles"]
    station_ids = stations.select(F.trim("id").alias("valid_station_id")).distinct()
    orphan_piles = piles.join(station_ids, F.trim(piles.station_id) == station_ids.valid_station_id, "left_anti")
    issue_frames.append(orphan_piles.select(F.lit(batch_id).alias("batch_id"), F.lit("charging_piles").alias("table_name"), "row_id", F.lit("DQ005").alias("rule_id"), F.lit("ERROR").alias("severity"), F.lit("station_id does not exist").alias("reason"), F.current_timestamp().alias("detected_at")))
    add("DQ006", "charging_piles", ~F.upper(F.trim("charge_type")).isin("FAST", "SLOW") | ~F.upper(F.trim("status")).isin("IDLE", "RESERVED", "CHARGING", "FAULT", "OFFLINE"), "pile enum is invalid")

    orders = frames["charging_orders"]
    order_dupes = orders.withColumn("key_count", F.count("*").over(Window.partitionBy(F.trim("order_no"))))
    issue_frames.append(order_dupes.where(F.col("key_count") > 1).select(F.lit(batch_id).alias("batch_id"), F.lit("charging_orders").alias("table_name"), "row_id", F.lit("DQ007").alias("rule_id"), F.lit("ERROR").alias("severity"), F.lit("duplicate order_no").alias("reason"), F.current_timestamp().alias("detected_at")))
    valid_users = users.select(F.trim("id").alias("uid")).distinct()
    valid_stations = stations.select(F.trim("id").alias("sid")).distinct()
    valid_piles = piles.select(F.trim("id").alias("pid"), F.trim("station_id").alias("pile_sid")).distinct()
    checked = (orders.alias("o").join(valid_users, F.trim("o.user_id") == valid_users.uid, "left")
               .join(valid_stations, F.trim("o.station_id") == valid_stations.sid, "left")
               .join(valid_piles, F.trim("o.pile_id") == valid_piles.pid, "left"))
    issue_frames.append(checked.where(F.col("uid").isNull() | F.col("sid").isNull() | F.col("pid").isNull() | (F.trim("o.station_id") != F.col("pile_sid"))).select(F.lit(batch_id).alias("batch_id"), F.lit("charging_orders").alias("table_name"), F.col("o.row_id").alias("row_id"), F.lit("DQ008").alias("rule_id"), F.lit("ERROR").alias("severity"), F.lit("order foreign key is invalid").alias("reason"), F.current_timestamp().alias("detected_at")))
    started, stopped, paid = parse_timestamp("started_at"), parse_timestamp("stopped_at"), parse_timestamp("paid_at")
    add("DQ009", "charging_orders", (started.isNotNull() & stopped.isNotNull() & (started > stopped)) | (paid.isNotNull() & stopped.isNotNull() & (paid < stopped)), "order event timeline is reversed")
    add("DQ010", "charging_orders", (F.col("duration_seconds").cast("long") < 0) | (F.col("energy_wh").cast("long") < 0) | (F.col("amount_cents").cast("long") < 0), "order measure is negative")
    complete = F.upper(F.trim("status")) == "COMPLETED"
    add("DQ011", "charging_orders", complete & (blank("paid_at") | blank("amount_cents") | blank("energy_wh")), "completed order lacks required values")
    expected_amount = F.round(F.col("energy_wh").cast("double") * F.col("price_cents_per_kwh").cast("double") / 1000)
    add("DQ012", "charging_orders", F.col("amount_cents").cast("double").isNotNull() & expected_amount.isNotNull() & (F.abs(F.col("amount_cents").cast("double") - expected_amount) > 1), "amount differs from energy times price")
    status = F.upper(F.trim("status"))
    add("DQ013", "charging_orders", ((status == "PENDING") & (started.isNotNull() | stopped.isNotNull() | paid.isNotNull())) | ((status == "CHARGING") & stopped.isNotNull()) | (complete & paid.isNull()), "status conflicts with lifecycle timestamps")

    recharges = frames["recharge_records"]
    recharge_checked = recharges.alias("r").join(valid_users, F.trim("r.user_id") == valid_users.uid, "left")
    issue_frames.append(recharge_checked.where(F.col("uid").isNull() | (F.col("r.amount_cents").cast("long") <= 0)).select(F.lit(batch_id).alias("batch_id"), F.lit("recharge_records").alias("table_name"), F.col("r.row_id").alias("row_id"), F.lit("DQ014").alias("rule_id"), F.lit("ERROR").alias("severity"), F.lit("recharge user or amount is invalid").alias("reason"), F.current_timestamp().alias("detected_at")))

    logs = frames["pile_status_logs"]
    states = ["IDLE", "RESERVED", "CHARGING", "FAULT", "OFFLINE"]
    allowed = ["IDLE>RESERVED", "RESERVED>CHARGING", "CHARGING>IDLE", "IDLE>FAULT", "FAULT>IDLE", "IDLE>OFFLINE", "OFFLINE>IDLE"]
    transition = F.concat(F.upper(F.trim("old_status")), F.lit(">"), F.upper(F.trim("new_status")))
    add("DQ015", "pile_status_logs", ~F.upper(F.trim("old_status")).isin(*states) | ~F.upper(F.trim("new_status")).isin(*states) | ~transition.isin(*allowed), "pile status transition is invalid")

    dirty_format_conditions = {
        "users": (F.col("status") != F.upper(F.trim("status"))) | (F.col("created_at") != F.trim("created_at")),
        "stations": (F.col("status") != F.upper(F.trim("status"))) | (F.col("created_at") != F.trim("created_at")),
        "charging_piles": (F.col("status") != F.upper(F.trim("status"))) | (F.col("charge_type") != F.upper(F.trim("charge_type"))),
        "charging_orders": (F.col("status") != F.upper(F.trim("status"))) | (F.col("created_at") != F.trim("created_at")),
        "recharge_records": F.col("created_at") != F.trim("created_at"),
        "pile_status_logs": (F.col("old_status") != F.upper(F.trim("old_status"))) | (F.col("new_status") != F.upper(F.trim("new_status"))),
    }
    for table, condition in dirty_format_conditions.items():
        add("DQ016", table, condition, "text or enum format is not canonical", "WARN")

    intervals = (orders.where(started.isNotNull() & stopped.isNotNull())
                 .withColumn("_start", started).withColumn("_stop", stopped)
                 .withColumn("_previous_stop", F.max("_stop").over(Window.partitionBy(F.trim("pile_id")).orderBy("_start", "row_id").rowsBetween(Window.unboundedPreceding, -1))))
    issue_frames.append(intervals.where(F.col("_previous_stop").isNotNull() & (F.col("_start") < F.col("_previous_stop"))).select(F.lit(batch_id).alias("batch_id"), F.lit("charging_orders").alias("table_name"), "row_id", F.lit("DQ017").alias("rule_id"), F.lit("ERROR").alias("severity"), F.lit("charging session overlaps an earlier session on the same pile").alias("reason"), F.current_timestamp().alias("detected_at")))
    rated = piles.select(F.trim("id").alias("rated_pid"), F.col("rated_power_w").cast("double").alias("rated_power_w"))
    powered = orders.alias("o").join(rated, F.trim("o.pile_id") == F.col("rated_pid"), "left")
    duration = F.unix_timestamp(parse_timestamp("o.stopped_at")) - F.unix_timestamp(parse_timestamp("o.started_at"))
    issue_frames.append(powered.where((F.col("rated_power_w") > 0) & (duration > 0) &
        (F.col("o.energy_wh").cast("double") > F.col("rated_power_w") * duration / 3600 + 1)).select(
        F.lit(batch_id).alias("batch_id"), F.lit("charging_orders").alias("table_name"),
        F.col("o.row_id").alias("row_id"), F.lit("DQ018").alias("rule_id"),
        F.lit("ERROR").alias("severity"),
        F.lit("charging energy exceeds pile rated power over the order interval").alias("reason"),
        F.current_timestamp().alias("detected_at")))
    return reduce(lambda left, right: left.unionByName(right), issue_frames).dropDuplicates(["batch_id", "table_name", "row_id", "rule_id"])


def main() -> None:
    from pyspark.sql import functions as F

    args = parse_args()
    batch_id = validate_batch_id(args.batch_id)
    spark = create_spark(f"MapForECar-Quality-{batch_id}", args.master)
    try:
        frames = {table: spark.read.parquet(batch_table_path(args.ods, f"ods_{table}", batch_id)) for table in TABLE_COLUMNS}
        # The rule branches form many tiny partitions.  Coalesce before
        # caching so every downstream action avoids materializing that small
        # file fan-out on a single-node HDFS installation.
        issues = detect(frames, batch_id).coalesce(2).cache()
        details_path = batch_table_path(args.output, "details", batch_id)
        summary_path = batch_table_path(args.output, "summary", batch_id)
        issues.write.mode("overwrite").parquet(details_path)
        totals = reduce(lambda left, right: left.unionByName(right), [frame.select(F.lit(table).alias("table_name"), "row_id") for table, frame in frames.items()]).groupBy("table_name").agg(F.count("*").alias("total_rows"))
        summary = (issues.groupBy("batch_id", "table_name", "rule_id", "severity")
                   .agg(F.count("*").alias("issue_count"), F.countDistinct("row_id").alias("affected_rows"), F.slice(F.sort_array(F.collect_set("row_id")), 1, 10).alias("sample_row_ids"))
                   .join(totals, "table_name").withColumn("issue_rate", F.col("affected_rows") / F.col("total_rows")))
        summary.write.mode("overwrite").parquet(summary_path)
        summary.orderBy("rule_id", "table_name").show(200, truncate=False)
        print(f"quality details: {issues.count()} rows -> {details_path}")
    finally:
        spark.stop()


if __name__ == "__main__":
    main()
